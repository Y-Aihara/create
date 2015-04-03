//------------------------------------------------------------------------------
// <copyright file="SpeechBasics.cpp" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma warning(disable : 4996)
// #define _CRT_SECURE_NO_WARNINGS

#include "stdafx.h"
#include <stdlib.h>	// console用
#include "resource.h"
#include "SpeechBasics.h"
#include <strsafe.h>

#include "rs232.h"
#include <mmsystem.h>

#include <stdio.h>
#include <time.h>

#define INITGUID
#include <guiddef.h>

HWND hWndApp;

RS232* rs232;

int gMode = 0;
char* direction = "0";
char* footMode = "R";
int serchCount = 0;

//	シーン遷移（メイシークラスとか作って組み込んだほうがよさそう）
enum Seen{
	fNotfound,
	fStart,
	fGreeted,
	fPassing,
	fFreetime
};

Seen seen = fNotfound;

static const float c_JointThickness = 3.0f;
static const float c_TrackedBoneThickness = 6.0f;
static const float c_InferredBoneThickness = 1.0f;
static const float c_HandSize = 30.0f;

FoundedBody foundedBody[BODY_COUNT];

// int 型要素を昇順ソートするときの比較関数
int IntCompareD(const void* pElem1, const void* pElem2)
{
	return *(const int*)pElem2 - *(const int*)pElem1;
}

// Static initializers
// LPCWSTR CSpeechBasics::GrammarFileName = L"SpeechBasics-D2D.grxml"
LPCWSTR CSpeechBasics::GrammarFileName = L"SpeechBasics-D2D-jp.grxml";

// This is the class ID we expect for the Microsoft Speech recognizer.
// Other values indicate that we're using a version of sapi.h that is
// incompatible with this sample.
DEFINE_GUID(CLSID_ExpectedRecognizer, 0x495648e7, 0xf7ab, 0x4267, 0x8e, 0x0f, 0xca, 0xfb, 0x7a, 0x33, 0xc1, 0x60);


/// <summary>
/// Entry point for the application
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="hPrevInstance">always 0</param>
/// <param name="lpCmdLine">command line arguments</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
/// <returns>status</returns>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE /* hPrevInstance */, _In_ LPWSTR /* lpCmdLine */, _In_ int nCmdShow)
{
	if (CLSID_ExpectedRecognizer != CLSID_SpInprocRecognizer){
		MessageBoxW(NULL, L"This sample was compiled against an incompatible version of sapi.h.\nPlease ensure that Microsoft Speech SDK and other sample requirements are installed and then rebuild application.", L"Missing requirements", MB_OK | MB_ICONERROR);

		return EXIT_FAILURE;
	}

	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

	// console

	AllocConsole();
	freopen("CON", "w", stdout);
	freopen("CON", "r", stdin);

	rs232 = new RS232(3, 921600, 8, PARITY_ODD, ONESTOPBIT);
	rs232->StartThread();

	srand((unsigned int)time(NULL));

	if (SUCCEEDED(hr)){
		{
			CSpeechBasics application;
			application.RunSp(hInstance, nCmdShow);
		}
	}

	FreeConsole();

	CoUninitialize();

	return EXIT_SUCCESS;
}


/// <summary>
/// Constructor
/// </summary>
CSpeechBasics::CSpeechBasics() :
m_pKinectSensor(NULL),
m_pAudioBeam(NULL),
m_pAudioStream(NULL),
m_p16BitAudioStream(NULL),
m_hSensorNotification(reinterpret_cast<WAITABLE_HANDLE>(INVALID_HANDLE_VALUE)),
m_pSpeechStream(NULL),
m_pSpeechRecognizer(NULL),
m_pSpeechContext(NULL),
m_pSpeechGrammar(NULL),
m_hSpeechEvent(INVALID_HANDLE_VALUE),
m_hWnd(NULL),
m_nStartTime(0),
m_nLastCounter(0),
m_nFramesSinceUpdate(0),
m_fFreq(0),
m_nNextStatusTime(0LL),
m_pCoordinateMapper(NULL),
m_pBodyFrameReader(NULL),
m_pD2DFactory(NULL),
m_pRenderTarget(NULL),
m_pBrushJointTracked(NULL),
m_pBrushJointInferred(NULL),
m_pBrushBoneTracked(NULL),
m_pBrushBoneInferred(NULL),
m_pBrushHandClosed(NULL),
m_pBrushHandOpen(NULL),
m_pBrushHandLasso(NULL),
//-------------------------------------------------------------------------
m_bSaveScreenshot(false),
m_pDepthFrameReader(NULL),
m_pDrawDepth(NULL),
m_pDepthRGBX(NULL)
{
	LARGE_INTEGER qpf = { 0 };
	if (QueryPerformanceFrequency(&qpf))														//	カウンタの周波数（更新頻度）
	{
		m_fFreq = double(qpf.QuadPart);															//	上で取った値をそのまま（上位だけ下位だけとかではない）
	}
}

/// <summary>
/// Destructor
/// </summary>
CSpeechBasics::~CSpeechBasics()
{

	//------------------------------------------------------------------------
	// clean up Direct2D renderer
	if (m_pDrawDepth){
		delete m_pDrawDepth;
		m_pDrawDepth = NULL;
	}

	if (m_pDepthRGBX){
		delete[] m_pDepthRGBX;
		m_pDepthRGBX = NULL;
	}
	//------------------------------------------------------------------------

	DiscardDirect2DResources();

	// clean up Direct2D
	SafeRelease(m_pD2DFactory);

	//-------------------------------------------------------------------------
	// done with depth frame reader
	SafeRelease(m_pDepthFrameReader);
	//-------------------------------------------------------------------------

	// done with body frame reader
	SafeRelease(m_pBodyFrameReader);

	// done with coordinate mapper
	SafeRelease(m_pCoordinateMapper);

	if (m_pKinectSensor)
	{
		m_pKinectSensor->Close();
	}

	//16 bit Audio Stream
	if (NULL != m_p16BitAudioStream)
	{
		delete m_p16BitAudioStream;
		m_p16BitAudioStream = NULL;
	}
	SafeRelease(m_pAudioStream);
	SafeRelease(m_pAudioBeam);
	SafeRelease(m_pKinectSensor);
}

/// <summary>
/// Creates the main window and begins processing
/// </summary>
/// <param name="hInstance">handle to the application instance</param>
/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
int CSpeechBasics::RunSp(HINSTANCE hInstance, int nCmdShow)
{
	MSG       msg = { 0 };
	WNDCLASS  wc;
	const int maxEventCount = 2;
	int eventCount = 1;
	HANDLE hEvents[maxEventCount];

	// Dialog custom window class
	ZeroMemory(&wc, sizeof(wc));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
	wc.lpfnWndProc = DefDlgProcW;
	wc.lpszClassName = L"BodyBasicsAppDlgWndClass";

	if (!RegisterClassW(&wc))
	{
		return 0;
	}

	// Create main application window
	hWndApp = CreateDialogParamW(
		hInstance,
		MAKEINTRESOURCE(IDD_APP),
		NULL,
		(DLGPROC)CSpeechBasics::MessageRouterSp,
		reinterpret_cast<LPARAM>(this));

	

	// Show window
	ShowWindow(hWndApp, nCmdShow);

	// Main message loop
	while (WM_QUIT != msg.message)
	{

		Update();

		if (m_hSpeechEvent != INVALID_HANDLE_VALUE)
		{
			hEvents[1] = m_hSpeechEvent;
			eventCount = 2;
		}

		hEvents[0] = reinterpret_cast<HANDLE>(m_hSensorNotification);

		// Check to see if we have either a message (by passing in QS_ALLINPUT)
		// Or sensor notification (hEvents[0])
		// Or a speech event (hEvents[1])
		DWORD waitResult = MsgWaitForMultipleObjectsEx(eventCount, hEvents, 50, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

		switch (waitResult)
		{
		case WAIT_OBJECT_0:
		{
			BOOLEAN sensorState = FALSE;

			// Getting the event data will reset the event.
			IIsAvailableChangedEventArgs* pEventData = nullptr;
			if (FAILED(m_pKinectSensor->GetIsAvailableChangedEventData(m_hSensorNotification, &pEventData)))
			{
				SetStatusMessage(L"Failed to get sensor availability.", 10000, true);
				break;
			}

			pEventData->get_IsAvailable(&sensorState);
			SafeRelease(pEventData);

			if (sensorState == FALSE)
			{
				SetStatusMessage(L"Sensor has been disconnected - attach Sensor", 10000, true);
			}
			else
			{
				HRESULT hr = S_OK;

				if (m_pSpeechRecognizer == NULL)
				{
					hr = InitializeSpeech();
				}
				if (SUCCEEDED(hr))
				{
					SetStatusMessage(L"Say: \"Forward\", \"Back\", \"Turn Left\" or \"Turn Right\"", 10000, true);
				}
				else
				{
					SetStatusMessage(L"Speech Initialization Failed", 10000, true);
				}
			}
		}
			break;
		case WAIT_OBJECT_0 + 1:
			if (eventCount == 2)
			{
					ProcessSpeech();
			}
			break;
		}

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			// If a dialog message will be taken care of by the dialog proc
			if ((hWndApp != NULL) && IsDialogMessageW(hWndApp, &msg))
			{
				continue;
			}
			GetMessage(&msg, NULL, 0, 0);

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		/*	これすると音声認識の精度が大きく落ちる，停止中にバッファに溜まったものの処理をできればうまくいくかも
		if (gMode == 1 && m_pSpeechRecognizer != NULL){
			m_pSpeechContext->Resume(0);
		}
		else if(m_pSpeechRecognizer != NULL){
			m_pSpeechContext->Pause(0);
		}
		*/
	}

	return static_cast<int>(msg.wParam);
}

/// <summary>
/// Handles window messages, passes most to the class instance to handle
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSpeechBasics::MessageRouterSp(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	CSpeechBasics* pThis = NULL;

	if (WM_INITDIALOG == uMsg)
	{
		pThis = reinterpret_cast<CSpeechBasics*>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<CSpeechBasics*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (NULL != pThis)
	{
		return pThis->DlgProcSp(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}

/// <summary>
/// Handle windows messages for the class instance
/// </summary>
/// <param name="hWnd">window message is for</param>
/// <param name="uMsg">message</param>
/// <param name="wParam">message data</param>
/// <param name="lParam">additional message data</param>
/// <returns>result of message processing</returns>
LRESULT CALLBACK CSpeechBasics::DlgProcSp(HWND hWnd, UINT message, WPARAM /* wParam */, LPARAM /* lParam */)
{
	LRESULT result = FALSE;

	switch (message)
	{
	case WM_INITDIALOG:
	{
		// Bind application window handle
		m_hWnd = hWnd;


		// Init Direct2D
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

		//-------------------------------------------------------------------------------
		// Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h)
		// We'll use this to draw the data we receive from the Kinect to the screen
		m_pDrawDepth = new ImageRenderer();
		HRESULT hr = m_pDrawDepth->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), m_pD2DFactory, cDepthWidth, cDepthHeight, cDepthWidth * sizeof(RGBQUAD));

		InitializeDefaultSensor();

		// Look for a connected Kinect, and create it if found
		hr = StartKinect();
		if (FAILED(hr))
		{
			break;
		}

		SetStatusMessage(L"Waiting for Sensor and Speech Initialization - Please ensure Sensor is attached.", 10000, true);
		result = FALSE;
		break;
	}

		// If the titlebar X is clicked, destroy app
	case WM_CLOSE:
		if (NULL != m_p16BitAudioStream)
		{
			m_p16BitAudioStream->SetSpeechState(false);
		}

		if (NULL != m_pSpeechRecognizer)
		{
			m_pSpeechRecognizer->SetRecoState(SPRST_INACTIVE_WITH_PURGE);

			//cleanup here
			SafeRelease(m_pSpeechStream);
			SafeRelease(m_pSpeechRecognizer);
			SafeRelease(m_pSpeechContext);
			SafeRelease(m_pSpeechGrammar);
		}

		DestroyWindow(hWnd);
		result = TRUE;
		break;

	case WM_DESTROY:
		// Quit the main message pump
		PostQuitMessage(0);
		result = TRUE;
		break;
	}

	return result;
}

/// <summary>
/// Open the KinectSensor and its Audio Stream
/// </summary>
/// <returns>S_OK on success, otherwise failure code.</returns>
HRESULT CSpeechBasics::StartKinect()
{
	HRESULT hr = S_OK;
	IAudioSource* pAudioSource = NULL;
	IAudioBeamList* pAudioBeamList = NULL;
	BOOLEAN sensorState = TRUE;

	hr = GetDefaultKinectSensor(&m_pKinectSensor);
	if (FAILED(hr))
	{
		SetStatusMessage(L"Failed getting default sensor!", 10000, true);
		return hr;
	}

	hr = m_pKinectSensor->SubscribeIsAvailableChanged(&m_hSensorNotification);

	if (SUCCEEDED(hr))
	{
		hr = m_pKinectSensor->Open();
	}

	if (SUCCEEDED(hr))
	{
		hr = m_pKinectSensor->get_AudioSource(&pAudioSource);
	}

	if (SUCCEEDED(hr))
	{
		hr = pAudioSource->get_AudioBeams(&pAudioBeamList);
	}

	if (SUCCEEDED(hr))
	{
		hr = pAudioBeamList->OpenAudioBeam(0, &m_pAudioBeam);
	}

	if (SUCCEEDED(hr))
	{
		hr = m_pAudioBeam->OpenInputStream(&m_pAudioStream);
		m_p16BitAudioStream = new KinectAudioStream(m_pAudioStream);
	}

	if (FAILED(hr))
	{
		SetStatusMessage(L"Failed opening an audio stream!", 10000, true);
	}

	SafeRelease(pAudioBeamList);
	SafeRelease(pAudioSource);
	return hr;
}

/// <summary>
/// Open the KinectSensor and its Audio Stream
/// </summary>
/// <returns>S_OK on success, otherwise failure code.</returns>
HRESULT CSpeechBasics::InitializeSpeech()
{

	// Audio Format for Speech Processing
	WORD AudioFormat = WAVE_FORMAT_PCM;
	WORD AudioChannels = 1;
	DWORD AudioSamplesPerSecond = 16000;
	DWORD AudioAverageBytesPerSecond = 32000;
	WORD AudioBlockAlign = 2;
	WORD AudioBitsPerSample = 16;

	WAVEFORMATEX wfxOut = { AudioFormat, AudioChannels, AudioSamplesPerSecond, AudioAverageBytesPerSecond, AudioBlockAlign, AudioBitsPerSample, 0 };

	HRESULT hr = CoCreateInstance(CLSID_SpStream, NULL, CLSCTX_INPROC_SERVER, __uuidof(ISpStream), (void**)&m_pSpeechStream);

	if (SUCCEEDED(hr))
	{

		m_p16BitAudioStream->SetSpeechState(true);
		hr = m_pSpeechStream->SetBaseStream(m_p16BitAudioStream, SPDFID_WaveFormatEx, &wfxOut);
	}

	if (SUCCEEDED(hr))
	{
		hr = CreateSpeechRecognizer();
	}

	if (FAILED(hr))
	{
		SetStatusMessage(L"Could not create speech recognizer. Please ensure that Microsoft Speech SDK and other sample requirements are installed.", 10000, true);
		return hr;
	}

	hr = LoadSpeechGrammar();

	if (FAILED(hr))
	{
		SetStatusMessage(L"Could not load speech grammar. Please ensure that grammar configuration file was properly deployed.", 10000, true);
		return hr;
	}

	hr = StartSpeechRecognition();

	if (FAILED(hr))
	{
		SetStatusMessage(L"Could not start recognizing speech.", 10000, true);
		return hr;
	}

	return hr;
}


/// <summary>
/// Create speech recognizer that will read Kinect audio stream data.
/// </summary>
/// <returns>
/// <para>S_OK on success, otherwise failure code.</para>
/// </returns>
HRESULT CSpeechBasics::CreateSpeechRecognizer()
{
	ISpObjectToken *pEngineToken = NULL;

	HRESULT hr = CoCreateInstance(CLSID_SpInprocRecognizer, NULL, CLSCTX_INPROC_SERVER, __uuidof(ISpRecognizer), (void**)&m_pSpeechRecognizer);

	if (SUCCEEDED(hr))
	{
		m_pSpeechRecognizer->SetInput(m_pSpeechStream, TRUE);

		// If this fails here, you have not installed the acoustic models for Kinect
		// hr = SpFindBestToken(SPCAT_RECOGNIZERS, L"Language=409;Kinect=True", NULL, &pEngineToken);
		hr = SpFindBestToken(SPCAT_RECOGNIZERS, L"Language=411;Kinect=True", NULL, &pEngineToken);

		if (SUCCEEDED(hr))
		{
			m_pSpeechRecognizer->SetRecognizer(pEngineToken);
			hr = m_pSpeechRecognizer->CreateRecoContext(&m_pSpeechContext);

			// For long recognition sessions (a few hours or more), it may be beneficial to turn off adaptation of the acoustic model. 
			// This will prevent recognition accuracy from degrading over time.
			if (SUCCEEDED(hr))
			{
				hr = m_pSpeechRecognizer->SetPropertyNum(L"AdaptationOn", 0);
			}
		}
	}
	SafeRelease(pEngineToken);
	return hr;
}

/// <summary>
/// Load speech recognition grammar into recognizer.
/// </summary>
/// <returns>
/// <para>S_OK on success, otherwise failure code.</para>
/// </returns>
HRESULT CSpeechBasics::LoadSpeechGrammar()
{
	HRESULT hr = m_pSpeechContext->CreateGrammar(1, &m_pSpeechGrammar);

	if (SUCCEEDED(hr))
	{
		// Populate recognition grammar from file
		hr = m_pSpeechGrammar->LoadCmdFromFile(GrammarFileName, SPLO_STATIC);
	}

	return hr;
}

/// <summary>
/// Start recognizing speech asynchronously.
/// </summary>
/// <returns>
/// <para>S_OK on success, otherwise failure code.</para>
/// </returns>
HRESULT CSpeechBasics::StartSpeechRecognition()
{
	HRESULT hr = S_OK;

	// Specify that all top level rules in grammar are now active
	hr = m_pSpeechGrammar->SetRuleState(NULL, NULL, SPRS_ACTIVE);
	if (FAILED(hr))
	{
		return hr;
	}

	// Specify that engine should always be reading audio
	hr = m_pSpeechRecognizer->SetRecoState(SPRST_ACTIVE_ALWAYS);
	if (FAILED(hr))
	{
		return hr;
	}

	// Specify that we're only interested in receiving recognition events
	hr = m_pSpeechContext->SetInterest(SPFEI(SPEI_RECOGNITION), SPFEI(SPEI_RECOGNITION));
	if (FAILED(hr))
	{
		return hr;
	}

	// Ensure that engine is recognizing speech and not in paused state
	hr = m_pSpeechContext->Resume(0);
	if (FAILED(hr))
	{
		return hr;
	}

	m_hSpeechEvent = m_pSpeechContext->GetNotifyEventHandle();
	return hr;
}

/// <summary>
/// Process recently triggered speech recognition events.
/// </summary>
void CSpeechBasics::ProcessSpeech()
{
	const float ConfidenceThreshold = 0.3f;
	// const float ConfidenceThreshold = 0.f;

	SPEVENT curEvent = { SPEI_UNDEFINED, SPET_LPARAM_IS_UNDEFINED, 0, 0, 0, 0 };
	ULONG fetched = 0;
	HRESULT hr = S_OK;
	LPWSTR pwszText;

	m_pSpeechContext->GetEvents(1, &curEvent, &fetched);
	while (fetched > 0)
	{
		switch (curEvent.eEventId)
		{
		case SPEI_RECOGNITION:
			if (SPET_LPARAM_IS_OBJECT == curEvent.elParamType)
			{
				// this is an ISpRecoResult
				ISpRecoResult* result = reinterpret_cast<ISpRecoResult*>(curEvent.lParam);
				SPPHRASE* pPhrase = NULL;

				hr = result->GetPhrase(&pPhrase);
				if (SUCCEEDED(hr))
				{
					if ((pPhrase->pProperties != NULL) && (pPhrase->pProperties->pFirstChild != NULL))
					{
						const SPPHRASEPROPERTY* pSemanticTag = pPhrase->pProperties->pFirstChild;
						if (pSemanticTag->SREngineConfidence > ConfidenceThreshold)
						{

							hr = result->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE, TRUE, &pwszText, NULL);

							int number = MapSpeechTagToAction(pSemanticTag->pszValue);


							/*=====================================================================================================================================
							//	<音声認識に応じた動作>
							======================================================================================================================================*/
							//	対話モード時限定
							if (gMode == 1){
								switch (number){
								case 1:
									printf("\n朝の挨拶\n\n");
									if (seen == fFreetime){
										PlaySound(_T("おはよう.wav"), NULL, SND_FILENAME);
									}
									break;
								case 2:
									printf("\n昼の挨拶\n\n");
									if (seen == fGreeted){
										seen = fPassing;
										PlaySound(_T("はじめまして.wav"), NULL, SND_FILENAME);
										rs232->write("A");
										PlaySound(_T("メイシーです.wav"), NULL, SND_FILENAME);
									}
									else if (seen == fFreetime){
										PlaySound(_T("こんにちは.wav"), NULL, SND_FILENAME);
									}
									break;
								case 3:
									printf("\n晩の挨拶\n\n");
									if (seen == fFreetime){
										PlaySound(_T("こんにちは.wav"), NULL, SND_FILENAME);
									}
									break;
								case 4:
									printf("\n出会いの挨拶\n\n");
									/*if (gMode == 1){
										rs232->write("A");
									}*/
									if (seen == fFreetime){
										PlaySound(_T("はじめまして.wav"), NULL, SND_FILENAME);
										PlaySound(_T("メイシーです.wav"), NULL, SND_FILENAME);
									}
									break;
								case 5:
									printf("\n名前呼ばれた\n\n");
									if (seen == fFreetime){
										PlaySound(_T("はーい！.wav"), NULL, SND_FILENAME);
									}
									break;
								case 6:
									printf("\n感謝感激\n\n");
									if (seen == fPassing){
										rs232->write("B");
										seen = fFreetime;
										// PlaySound(_T("僕と遊んでよ.wav"), NULL, SND_FILENAME);てきな
									}
									else if (seen == fFreetime){
										PlaySound(_T("どういたしまして.wav"), NULL, SND_FILENAME);
									}
									break;
								case 7:
									printf("\n別れの挨拶\n\n");
									if (seen == fFreetime){
										// PlaySound(_T("もう大丈夫だよ"), NULL, SND_FILENAME);てきな
										PlaySound(_T("ばいばーい！.wav"), NULL, SND_FILENAME);
										seen = fNotfound;
									}
									break;
								case 8:
									printf("\n疲労困憊\n\n");
									if (seen == fFreetime){
										PlaySound(_T("がんばって！.wav"), NULL, SND_FILENAME);
									}
									break;
								case 9:
									printf("\n無茶ぶり\n\n");
									if (seen == fFreetime){
										// PlaySound(_T("おもしろいはなし.wav"), NULL, SND_FILENAME);てきな
										PlaySound(_T("はーい！.wav"), NULL, SND_FILENAME);
									}
									break;
								}
							}
							/*=====================================================================================================================================
							//	<\音声認識に応じた動作>
							======================================================================================================================================*/
						}
					}
					::CoTaskMemFree(pPhrase);
				}
			}
			break;
		}

		m_pSpeechContext->GetEvents(1, &curEvent, &fetched);
	}

	return;
}

/// <summary>
/// Maps a specified speech semantic tag to the corresponding action to be performed on turtle.
/// </summary>
/// <returns>
/// Action that matches <paramref name="pszSpeechTag"/>, or TurtleActionNone if no matches were found.
/// </returns>

int CSpeechBasics::MapSpeechTagToAction(LPCWSTR pszSpeechTag)
{
	int number = -1;

	struct SpeechTagToAction
	{
		LPCWSTR pszSpeechTag;
		int number;
	};
	const SpeechTagToAction Map[] =
	{
		{ L"GOODMORNING", 1 },
		{ L"HELLO", 2 },
		{ L"GOODEVENING", 3 },
		{ L"NICETOMEETYOU", 4 },
		{ L"CALLEDNAME", 5 },
		{ L"THANKYOU", 6 },
		{ L"SEEYOU", 7 },
		{ L"MEGETERU", 8 },
		{ L"MUCHABURI", 9 },
	};

	// TurtleAction action = TurtleActionNone;

	for (int i = 0; i < _countof(Map); ++i)
	{
		if ((Map[i].pszSpeechTag != NULL) && (0 == wcscmp(Map[i].pszSpeechTag, pszSpeechTag)))
		{
			number = Map[i].number;
			break;
		}
	}

	return number;
}


/// <summary>
/// Set the status bar message
/// </summary>
/// <param name="szMessage">message to display</param>
/// <param name="showTimeMsec">time in milliseconds to ignore future status messages</param>
/// <param name="bForce">force status update</param>
bool CSpeechBasics::SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce)
{
	INT64 now = GetTickCount64();

	if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
	{
		SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
		m_nNextStatusTime = now + nShowTimeMsec;

		return true;
	}

	return false;
}

void CSpeechBasics::Update()
{
	if (!m_pBodyFrameReader)
	{
		return;
	}

	//---------------------------------------------------------------------------
	if (!m_pDepthFrameReader){
		return;
	}
	//---------------------------------------------------------------------------
	IBodyFrame* pBodyFrame = NULL;
	//---------------------------------------------------------------------------
	IDepthFrame* pDepthFrame = NULL;
	//---------------------------------------------------------------------------


	HRESULT hr = m_pBodyFrameReader->AcquireLatestFrame(&pBodyFrame);
	//---------------------------------------------------------------------------
	// HRESULT hr2 = m_pDepthFrameReader->AcquireLatestFrame(&pDepthFrame);

	if (SUCCEEDED(hr))
	{
		INT64 nTime = 0;

		hr = pBodyFrame->get_RelativeTime(&nTime);												//	フレーム取得

		IBody* ppBodies[BODY_COUNT] = { 0 };

		if (SUCCEEDED(hr))
		{
			hr = pBodyFrame->GetAndRefreshBodyData(_countof(ppBodies), ppBodies);				//	データ取得
		}

		if (SUCCEEDED(hr))
		{
			ProcessBody(nTime, BODY_COUNT, ppBodies);											//	メイン処理
		}

		for (int i = 0; i < _countof(ppBodies); ++i)
		{
			SafeRelease(ppBodies[i]);
		}

	}
	SafeRelease(pBodyFrame);

	hr = m_pDepthFrameReader->AcquireLatestFrame(&pDepthFrame);

	if (SUCCEEDED(hr))
	{

		// 仮
		INT64 nTimeD = 0;
		IFrameDescription* pFrameDescription = NULL;
		int nWidth = 0;
		int nHeight = 0;
		USHORT nDepthMinReliableDistance = 0;
		USHORT nDepthMaxDistance = 0;
		UINT nBufferSize = 0;
		UINT16 *pBuffer = NULL;

		hr = pDepthFrame->get_RelativeTime(&nTimeD);

		if (SUCCEEDED(hr))
		{
			hr = pDepthFrame->get_FrameDescription(&pFrameDescription);
		}
		if (SUCCEEDED(hr))
		{
			hr = pFrameDescription->get_Width(&nWidth);
		}
		if (SUCCEEDED(hr))
		{
			hr = pFrameDescription->get_Height(&nHeight);
		}
		if (SUCCEEDED(hr))
		{
			hr = pDepthFrame->get_DepthMinReliableDistance(&nDepthMinReliableDistance);
		}
		if (SUCCEEDED(hr))
		{
			nDepthMaxDistance = USHRT_MAX;
		}
		if (SUCCEEDED(hr))
		{
			hr = pDepthFrame->AccessUnderlyingBuffer(&nBufferSize, &pBuffer);
		}
		//-------------------------------------------------------------------------------
		if (SUCCEEDED(hr))
		{
				for (int i = 0; i < 6; i++){
					if (foundedBody[i].tof == true){
						int index = (int)((int)foundedBody[i].positionX + ((int)foundedBody[i].positionY * nWidth));
						if (-100 * nWidth <= index && index < nWidth * nHeight){
							foundedBody[i].depth = pBuffer[index];
							// printf("\nindex:%d\n\n", index);
						}
						else{
							printf("エラーエラーエラー\n");
							printf("index:%d\n", index);
						}
					}
					else{
						foundedBody[i].depth = 0;
						foundedBody[i].positionX = 0;
						foundedBody[i].positionY = 0;
						foundedBody[i].hPositionX = 0;
						foundedBody[i].hPositionY = -100;
					}
				}
				/*
				for (int i = 0; i < BODY_COUNT; i++){
					if (foundedBody[i].tof == true){
						printf("%d:(%d, %d), %d\n", i, (int)foundedBody[i].positionX, (int)foundedBody[i].positionY, (int)foundedBody[i].depth);
					}
					else{
						printf("%d:(%d, %d), %d\n", i, (int)foundedBody[i].positionX, (int)foundedBody[i].positionY, (int)foundedBody[i].depth);
					}
				}
				*/

				int temp[BODY_COUNT];
				int sercher[BODY_COUNT];

				for (int i = 0; i < BODY_COUNT; i++){
					temp[i] = (int)foundedBody[i].positionX;
				}

				qsort(temp, BODY_COUNT, sizeof(*temp), IntCompareD);

				/*
				for (int i = 0; i < 6; i++){
					printf("%d", temp[i]);
				}
				printf("\n");
				*/

				sercher[0] = (nWidth - temp[0]) + (temp[0] - temp[1]);

				for (int i = 1; i < BODY_COUNT - 1; i++){
					sercher[i] = (temp[i - 1] - temp[i]) + (temp[i] - temp[i + 1]);
				}

				sercher[5] = (temp[4] - temp[5]) + (temp[5] - 0);

				/*
				for (int i = 0; i < 6; i++){
					printf("%d", sercher[i]);
				}
				printf("\n");
				*/

				int max = 0;
				for (int i = 0; i < BODY_COUNT; i++){
					if (max < sercher[i]){
						max = sercher[i];
					}
				}

				// printf("max:%d\n", max);

				int target, target2;
				for (int i = 0; i < BODY_COUNT; i++){
					if (max == sercher[i]){
						target = i;
					}
				}

				// printf("%d, ", temp[target]);
				// printf("%d\n", target);

				for (int i = 0; i < BODY_COUNT; i++){
					if (temp[target] == (int)foundedBody[i].positionX){
						target2 = i;
					}
				}

				int writeFlag = 0;


				// char direction = 0;

				if (-99 < foundedBody[target2].hPositionY && foundedBody[target2].hPositionY < 140){
					if (0 < foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 102){
						if (strcmp(direction, "0") != 0){
							// rs232->write("0");
							writeFlag = 1;
							direction = "0";
						}
					}
					else if (102 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 204){
						if (strcmp(direction, "1") != 0){
							// rs232->write("1");
							writeFlag = 1;
							direction = "1";
						}
					}
					else if (204 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 308){
						if (strcmp(direction, "2") != 0){
							// rs232->write("2");
							writeFlag = 1;
							direction = "2";
						}
					}
					else if (308 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 410){
						if (strcmp(direction, "3") != 0){
							// rs232->write("3");
							writeFlag = 1;
							direction = "3";
						}
					}
					else if (410 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 512){
						if (strcmp(direction, "4") != 0){
							// rs232->write("4");
							writeFlag = 1;
							direction = "4";
						}
					}
					else{
						//	direction = "5";
					}
				}
				else if (140 <= foundedBody[target2].hPositionY && foundedBody[target2].hPositionY <= 240){
					writeFlag = 1;
					if (0 < foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 102){
						if (strcmp(direction, "5") != 0){
							// rs232->write("5");
							writeFlag = 1;
							direction = "5";
						}
					}
					else if (102 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 204){
						if (strcmp(direction, "6") != 0){
							// rs232->write("6");
							writeFlag = 1;
							direction = "6";
						}
					}
					else if (204 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 308){
						if (strcmp(direction, "7") != 0){
							// rs232->write("7");
							writeFlag = 1;
							direction = "7";
						}
					}
					else if (308 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 410){
						if (strcmp(direction, "8") != 0){
							// rs232->write("8");
							writeFlag = 1;
							direction = "8";
						}
					}
					else if (410 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 512){
						if (strcmp(direction, "9") != 0){
							// rs232->write("9");
							writeFlag = 1;
							direction = "9";
						}
					}
					else{
						//	direction = "5";
					}
				}
				else if (240 <= foundedBody[target2].hPositionY && foundedBody[target2].hPositionY <= 424){
					if (0 < foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 102){
						if (strcmp(direction, "10") != 0){
							// rs232->write("10");
							writeFlag = 1;
							direction = "10";
						}
					}
					else if (102 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX < 204){
						if (strcmp(direction, "11") != 0){
							// rs232->write("11");
							writeFlag = 1;
							direction = "11";
						}
					}
					else if (204 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 308){
						if (strcmp(direction, "12") != 0){
							// rs232->write("12");
							writeFlag = 1;
							direction = "12";
						}
					}
					else if (308 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 410){
						if (strcmp(direction, "13") != 0){
							// rs232->write("13");
							writeFlag = 1;
							direction = "13";
						}
					}
					else if (410 <= foundedBody[target2].hPositionX && foundedBody[target2].hPositionX <= 512){
						if (strcmp(direction, "14") != 0){
							// rs232->write("14");
							writeFlag = 1;
							direction = "14";
						}
					}
					else{
						//	direction = "5";
					}
					//	direction = "5";
				}


				/*=====================================================================================================================================
				//	<状態表示>
				======================================================================================================================================*/
				/*
				printf("人数:%d\n", FoundedBody::number);

				printf("target:%d, position:(%f, %f), depth:%d\n", target2, foundedBody[target2].positionX, foundedBody[target2].positionY, (int)foundedBody[target2].depth);

				printf("hPosition:(%f, %f)", foundedBody[target2].hPositionX, foundedBody[target2].hPositionY);
				printf("hDirection:%s\n", direction);

				EnterCriticalSection(&rs232->cs);
				printf("%s\n", rs232->gStr);
				LeaveCriticalSection(&rs232->cs);
				*/
				/*=====================================================================================================================================
				//	<\状態表示>
				======================================================================================================================================*/

				/*=====================================================================================================================================
				//	<シリアル通信>（音声認識の動作は別途上で）
				======================================================================================================================================*/

				if (FoundedBody::number != 0){
					if (foundedBody[target2].depth < 900){	//	対話モード
						// printf("対話モード\n\n");
						// hr = m_pSpeechContext->Resume(0);
						//	顔の向き，表示 - 頭の座標（下でまとめて）
						//  腕             - 音声に応じて
						//  足             - 止める
						if (writeFlag != 1 && strcmp(footMode, "S") != 0){	//	モードチェンジが細かくなってシリアル送信が連続してしまうとモードの更新がされない場合がある
							rs232->write("E");
							footMode = "S";
						}

						/*=====================================================================================================================================
						//	<シーン毎の動作>
						======================================================================================================================================*/
						//	要追記←ではなかったっぽい
						//	音声認識部で処理してる
						/*=====================================================================================================================================
						//	<\シーン毎の動作>
						======================================================================================================================================*/

						if (gMode != 1){
							printf("対話モード\n\n");
							PlaySound(_T("こんにちは.wav"), NULL, SND_FILENAME);
							seen = fGreeted;
						}
						gMode = 1;
						// hr = m_pSpeechContext->Pause(0);
					}
					else{									//	追跡モード
						// printf("追跡モード：");
						//	顔の向き，表示 - 頭の座標（下でまとめて）
						//	足             - 追跡
							if (0 < foundedBody[target2].positionX && foundedBody[target2].positionX < 204){
								if (writeFlag != 1 && strcmp(footMode, "R") != 0){
									rs232->write("R");
									footMode = "R";
								}
							}
							else if (204 <= foundedBody[target2].positionX && foundedBody[target2].positionX < 308){
								if (writeFlag != 1 && strcmp(footMode, "F") != 0){
									rs232->write("C");
									footMode = "F";
								}
							}
							else if (308 <= foundedBody[target2].positionX && foundedBody[target2].positionX <= 512){
								if (writeFlag != 1 && strcmp(footMode, "L") != 0){
									rs232->write("L");
									footMode = "L";
								}
							}
							else{
								printf("\n\n\n\n\n\n\n");
								//	rs232->write("足停止");
							}
						//	rs232->write("足追跡");
						if (gMode != 2){
							seen = fNotfound;
							printf("追跡モード\n\n");
						}
						gMode = 2;
					}
					//	顔の向き，表示（共通）
					//	rs232->write(direction);
				}
				else{										//	探索モード
					// printf("探索モード：");
					//	顔の向き，表示     - ランダム？
					if (serchCount == 25){
						int random = rand() % 15;
						if (writeFlag != 1 && random == 0){
							direction = "0";
							// rs232->write("0");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 1){
							direction = "1";
							// rs232->write("1");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 2){
							direction = "2";
							// rs232->write("2");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 3){
							direction = "3";
							// rs232->write("3");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 4){
							direction = "4";
							// rs232->write("4");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 5){
							direction = "5";
							// rs232->write("5");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 6){
							direction = "6";
							// rs232->write("6");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 7){
							direction = "7";
							// rs232->write("7");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 8){
							direction = "8";
							// rs232->write("8");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 9){
							direction = "9";
							// rs232->write("9");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 10){
							direction = "10";
							// rs232->write("10");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 11){
							direction = "11";
							// rs232->write("11");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 12){
							direction = "12";
							// rs232->write("12");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 13){
							direction = "13";
							// rs232->write("13");
							writeFlag = 1;
						}
						else if (writeFlag != 1 && random == 14){
							direction = "14";
							// rs232->write("14");
							writeFlag = 1;
						}
						else{

						}
					}
					//	足                 - ランダム？
					if (serchCount == 50){
						if (writeFlag != 1 && (rand() % 2) == 0){
							// footMode = "R";
							// rs232->write("R");
							rs232->write("E");
							footMode = "S";
						}
						else if (writeFlag != 1){
							// footMode = "L";
							// rs232->write("L");
							rs232->write("E");
							footMode = "S";
						}
						serchCount = 0;
					}
					if (gMode != 3){
						seen = fNotfound;
						printf("探索モード\n\n");
					}
					//	rs232->write(footMode);
					gMode = 3;
					serchCount++;
				}
				
				if (writeFlag == 1){
					rs232->write(direction);
				}
				

				/*=====================================================================================================================================
				//	<\シリアル通信>
				======================================================================================================================================*/
		}
		else{
			printf("error\n");
		}
		//---------------------------------------------------------------------------------------
		SafeRelease(pFrameDescription);
	}

	// SafeRelease(pBodyFrame);
	//------------------------------------------------------------------------------------------------
	SafeRelease(pDepthFrame);
}

HRESULT CSpeechBasics::InitializeDefaultSensor()
{
	HRESULT hr;

	hr = GetDefaultKinectSensor(&m_pKinectSensor);
	if (FAILED(hr))
	{
		return hr;
	}

	if (m_pKinectSensor)
	{
		// Initialize the Kinect and get coordinate mapper and the body reader
		IBodyFrameSource* pBodyFrameSource = NULL;
		//-----------------------------------------------------------------------------------
		IDepthFrameSource* pDepthFrameSource = NULL;
		//-----------------------------------------------------------------------------------

		hr = m_pKinectSensor->Open();

		if (SUCCEEDED(hr))
		{
			hr = m_pKinectSensor->get_CoordinateMapper(&m_pCoordinateMapper);
		}

		if (SUCCEEDED(hr))
		{
			hr = m_pKinectSensor->get_BodyFrameSource(&pBodyFrameSource);
		}

		//-----------------------------------------------------------------------------------
		if (SUCCEEDED(hr)){
			hr = m_pKinectSensor->get_DepthFrameSource(&pDepthFrameSource);
		}
		//-----------------------------------------------------------------------------------


		if (SUCCEEDED(hr))
		{
			hr = pBodyFrameSource->OpenReader(&m_pBodyFrameReader);
		}

		//-----------------------------------------------------------------------------------
		if (SUCCEEDED(hr)){
			hr = pDepthFrameSource->OpenReader(&m_pDepthFrameReader);
		}
		//-----------------------------------------------------------------------------------

		SafeRelease(pBodyFrameSource);

		//-----------------------------------------------------------------------------------
		SafeRelease(pDepthFrameSource);
		//-----------------------------------------------------------------------------------
	}

	if (!m_pKinectSensor || FAILED(hr))
	{
		SetStatusMessage(L"No ready Kinect found!", 10000, true);
		return E_FAIL;
	}

	return hr;
}

void CSpeechBasics::ProcessBody(INT64 nTime, int nBodyCount, IBody** ppBodies)
{
	if (m_hWnd)
	{
		HRESULT hr = EnsureDirect2DResources();

		if (SUCCEEDED(hr) && m_pRenderTarget && m_pCoordinateMapper)
		{
			//	FoundedBody foundedBody[BODY_COUNT];

			m_pRenderTarget->BeginDraw();
			m_pRenderTarget->Clear();

			RECT rct;
			GetClientRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rct);								//	GetClientRectでrctにウインドウのスクリーン座標を入れる，GetDlgItemがコントロール（ウインドウハンドルの一部？）を返す
			int width = rct.right;
			int height = rct.bottom;

			FoundedBody::number = 0;

			for (int i = 0; i < nBodyCount; ++i)
			{
				foundedBody[i].tof = false;
				IBody* pBody = ppBodies[i];
				if (pBody)
				{

					BOOLEAN bTracked = false;
					hr = pBody->get_IsTracked(&bTracked);

					if (SUCCEEDED(hr) && bTracked)
					{
						Joint joints[JointType_Count];											//	関節の種類，カメラ空間での座標，トラッキング情報
						D2D1_POINT_2F jointPoints[JointType_Count];								//	float型でX，Y座標（今はスクリーン座標が入る）
						HandState leftHandState = HandState_Unknown;							//	Unknown, NotTracked, Open, Closed, Lasso（列挙型）
						HandState rightHandState = HandState_Unknown;
						// スクリーン座標
						float HeadX, HeadY;
						float SpineX, SpineY;

						pBody->get_HandLeftState(&leftHandState);
						pBody->get_HandRightState(&rightHandState);

						hr = pBody->GetJoints(_countof(joints), joints);						//	関節数分関節情報取得
						if (SUCCEEDED(hr))
						{
							for (int j = 0; j < _countof(joints); ++j)
							{
								jointPoints[j] = BodyToScreen(joints[j].Position, width, height);	//	下にある，スクリーン座標	この配列データ重要
							}

							DrawBody(joints, jointPoints);											//	骨格表示

							DrawHand(leftHandState, jointPoints[JointType_HandLeft]);				//	手のステータスに合わせた色の円を表示
							DrawHand(rightHandState, jointPoints[JointType_HandRight]);

							HeadX = jointPoints[JointType_Head].x;
							HeadY = jointPoints[JointType_Head].y;
							SpineX = jointPoints[JointType_SpineBase].x;
							SpineY = jointPoints[JointType_SpineBase].y;

							//	printf("Head(%f, %f)/Spine(%f, %f)\n", HeadX, HeadY, SpineX, SpineY);

							foundedBody[i].tof = true;
							FoundedBody::number++;

							foundedBody[i].hPositionX = HeadX;
							foundedBody[i].hPositionY = HeadY;
							foundedBody[i].positionX = (HeadX + SpineX) / 2;
							foundedBody[i].positionY = (HeadY + SpineY) / 2;
						}
					}
				}

			}

			hr = m_pRenderTarget->EndDraw();

			// Device lost, need to recreate the render target
			// We'll dispose it now and retry drawing
			if (D2DERR_RECREATE_TARGET == hr)
			{
				hr = S_OK;
				DiscardDirect2DResources();
			}
		}

		if (!m_nStartTime)
		{
			m_nStartTime = nTime;
		}

		double fps = 0.0;

		LARGE_INTEGER qpcNow = { 0 };
		if (m_fFreq)
		{
			if (QueryPerformanceCounter(&qpcNow))
			{
				if (m_nLastCounter)
				{
					m_nFramesSinceUpdate++;
					fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
				}
			}
		}

		WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f    Time = %I64d", fps, (nTime - m_nStartTime));

		if (SetStatusMessage(szStatusMessage, 1000, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFramesSinceUpdate = 0;
		}
	}
}

/// <summary>
/// Ensure necessary Direct2d resources are created
/// </summary>
/// <returns>S_OK if successful, otherwise an error code</returns>
HRESULT CSpeechBasics::EnsureDirect2DResources()
{
	HRESULT hr = S_OK;

	if (m_pD2DFactory && !m_pRenderTarget)
	{
		RECT rc;
		GetWindowRect(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), &rc);

		int width = rc.right - rc.left;
		int height = rc.bottom - rc.top;
		D2D1_SIZE_U size = D2D1::SizeU(width, height);
		D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
		rtProps.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE);
		rtProps.usage = D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE;

		// Create a Hwnd render target, in order to render to the window set in initialize
		hr = m_pD2DFactory->CreateHwndRenderTarget(
			rtProps,
			D2D1::HwndRenderTargetProperties(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), size),
			&m_pRenderTarget
			);

		if (FAILED(hr))
		{
			SetStatusMessage(L"Couldn't create Direct2D render target!", 10000, true);
			return hr;
		}

		// light green
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.27f, 0.75f, 0.27f), &m_pBrushJointTracked);

		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Yellow, 1.0f), &m_pBrushJointInferred);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 1.0f), &m_pBrushBoneTracked);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Gray, 1.0f), &m_pBrushBoneInferred);

		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red, 0.5f), &m_pBrushHandClosed);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Green, 0.5f), &m_pBrushHandOpen);
		m_pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Blue, 0.5f), &m_pBrushHandLasso);
	}

	return hr;
}

/// <summary>
/// Dispose Direct2d resources 
/// </summary>
void CSpeechBasics::DiscardDirect2DResources()
{
	SafeRelease(m_pRenderTarget);

	SafeRelease(m_pBrushJointTracked);
	SafeRelease(m_pBrushJointInferred);
	SafeRelease(m_pBrushBoneTracked);
	SafeRelease(m_pBrushBoneInferred);

	SafeRelease(m_pBrushHandClosed);
	SafeRelease(m_pBrushHandOpen);
	SafeRelease(m_pBrushHandLasso);
}


//	あやしい
/// <summary>
/// Converts a body point to screen space
/// </summary>
/// <param name="bodyPoint">body point to tranform</param>
/// <param name="width">width (in pixels) of output buffer</param>
/// <param name="height">height (in pixels) of output buffer</param>
/// <returns>point in screen-space</returns>
D2D1_POINT_2F CSpeechBasics::BodyToScreen(const CameraSpacePoint& bodyPoint, int width, int height)
{
	// Calculate the body's position on the screen
	DepthSpacePoint depthPoint = { 0 };
	m_pCoordinateMapper->MapCameraPointToDepthSpace(bodyPoint, &depthPoint);
	/*
	float screenPointX = static_cast<float>(depthPoint.X * width) / cDepthWidth;
	float screenPointY = static_cast<float>(depthPoint.Y * height) / cDepthHeight;

	return D2D1::Point2F(screenPointX, screenPointY);
	*/
	return D2D1::Point2F(depthPoint.X, depthPoint.Y);
}

/// <summary>
/// Draws a body 
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
void CSpeechBasics::DrawBody(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints)
{
	// Draw the bones

	// Torso
	DrawBone(pJoints, pJointPoints, JointType_Head, JointType_Neck);
	DrawBone(pJoints, pJointPoints, JointType_Neck, JointType_SpineShoulder);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_SpineMid);
	DrawBone(pJoints, pJointPoints, JointType_SpineMid, JointType_SpineBase);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderRight);
	DrawBone(pJoints, pJointPoints, JointType_SpineShoulder, JointType_ShoulderLeft);
	DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipRight);
	DrawBone(pJoints, pJointPoints, JointType_SpineBase, JointType_HipLeft);

	// Right Arm    
	DrawBone(pJoints, pJointPoints, JointType_ShoulderRight, JointType_ElbowRight);
	DrawBone(pJoints, pJointPoints, JointType_ElbowRight, JointType_WristRight);
	DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_HandRight);
	DrawBone(pJoints, pJointPoints, JointType_HandRight, JointType_HandTipRight);
	DrawBone(pJoints, pJointPoints, JointType_WristRight, JointType_ThumbRight);

	// Left Arm
	DrawBone(pJoints, pJointPoints, JointType_ShoulderLeft, JointType_ElbowLeft);
	DrawBone(pJoints, pJointPoints, JointType_ElbowLeft, JointType_WristLeft);
	DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_HandLeft);
	DrawBone(pJoints, pJointPoints, JointType_HandLeft, JointType_HandTipLeft);
	DrawBone(pJoints, pJointPoints, JointType_WristLeft, JointType_ThumbLeft);

	// Right Leg
	DrawBone(pJoints, pJointPoints, JointType_HipRight, JointType_KneeRight);
	DrawBone(pJoints, pJointPoints, JointType_KneeRight, JointType_AnkleRight);
	DrawBone(pJoints, pJointPoints, JointType_AnkleRight, JointType_FootRight);

	// Left Leg
	DrawBone(pJoints, pJointPoints, JointType_HipLeft, JointType_KneeLeft);
	DrawBone(pJoints, pJointPoints, JointType_KneeLeft, JointType_AnkleLeft);
	DrawBone(pJoints, pJointPoints, JointType_AnkleLeft, JointType_FootLeft);

	// Draw the joints
	for (int i = 0; i < JointType_Count; ++i)
	{
		D2D1_ELLIPSE ellipse = D2D1::Ellipse(pJointPoints[i], c_JointThickness, c_JointThickness);

		if (pJoints[i].TrackingState == TrackingState_Inferred)
		{
			m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointInferred);
		}
		else if (pJoints[i].TrackingState == TrackingState_Tracked)
		{
			m_pRenderTarget->FillEllipse(ellipse, m_pBrushJointTracked);
		}
	}
}

/// <summary>
/// Draws one bone of a body (joint to joint)
/// </summary>
/// <param name="pJoints">joint data</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="pJointPoints">joint positions converted to screen space</param>
/// <param name="joint0">one joint of the bone to draw</param>
/// <param name="joint1">other joint of the bone to draw</param>
void CSpeechBasics::DrawBone(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints, JointType joint0, JointType joint1)
{
	TrackingState joint0State = pJoints[joint0].TrackingState;
	TrackingState joint1State = pJoints[joint1].TrackingState;

	// If we can't find either of these joints, exit
	if ((joint0State == TrackingState_NotTracked) || (joint1State == TrackingState_NotTracked))
	{
		return;
	}

	// Don't draw if both points are inferred
	if ((joint0State == TrackingState_Inferred) && (joint1State == TrackingState_Inferred))
	{
		return;
	}

	// We assume all drawn bones are inferred unless BOTH joints are tracked
	if ((joint0State == TrackingState_Tracked) && (joint1State == TrackingState_Tracked))
	{
		m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneTracked, c_TrackedBoneThickness);
	}
	else
	{
		m_pRenderTarget->DrawLine(pJointPoints[joint0], pJointPoints[joint1], m_pBrushBoneInferred, c_InferredBoneThickness);
	}
}

/// <summary>
/// Draws a hand symbol if the hand is tracked: red circle = closed, green circle = opened; blue circle = lasso
/// </summary>
/// <param name="handState">state of the hand</param>
/// <param name="handPosition">position of the hand</param>
void CSpeechBasics::DrawHand(HandState handState, const D2D1_POINT_2F& handPosition)
{
	D2D1_ELLIPSE ellipse = D2D1::Ellipse(handPosition, c_HandSize, c_HandSize);

	switch (handState)
	{
	case HandState_Closed:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandClosed);
		break;

	case HandState_Open:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandOpen);
		break;

	case HandState_Lasso:
		m_pRenderTarget->FillEllipse(ellipse, m_pBrushHandLasso);
		break;
	}
}


int FoundedBody::number = 0;