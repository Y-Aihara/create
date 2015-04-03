//------------------------------------------------------------------------------
// <copyright file="SpeechBasics.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//------------------------------------------------------------------------------

#pragma once

#include "KinectAudioStream.h"
// #include "BodyBasics.h"
#include "resource.h"

#include "ImageRenderer.h"

// For FORMAT_WaveFormatEx and such
#include <uuids.h>

// For speech APIs
// NOTE: To ensure that application compiles and links against correct SAPI versions (from Microsoft Speech
//       SDK), VC++ include and library paths should be configured to list appropriate paths within Microsoft
//       Speech SDK installation directory before listing the default system include and library directories,
//       which might contain a version of SAPI that is not appropriate for use together with Kinect sensor.
#include <sapi.h>
__pragma(warning(push))
__pragma(warning(disable:6385 6001)) // Suppress warnings in public SDK header
#include <sphelper.h>
__pragma(warning(pop))


class FoundedBody{
public:
	static int number;
	bool tof = false;
	float hPositionX = 0;
	float hPositionY = 0;
	float positionX = 0;
	float positionY = 0;
	UINT16 depth = 0;
};

/// <summary>
/// Main application class for SpeechBasics sample.
/// </summary>
class CSpeechBasics 
{

	static const int        cDepthWidth = 512;
	static const int        cDepthHeight = 424;

public:

	/// <summary>
	/// Constructor
	/// </summary>
	CSpeechBasics();

	/// <summary>
	/// Destructor
	/// </summary>
	~CSpeechBasics();

	/// <summary>
	/// Handles window messages, passes most to the class instance to handle
	/// </summary>
	/// <param name="hWnd">window message is for</param>
	/// <param name="uMsg">message</param>
	/// <param name="wParam">message data</param>
	/// <param name="lParam">additional message data</param>
	/// <returns>result of message processing</returns>
	static LRESULT CALLBACK MessageRouterSp(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	/// <summary>
	/// Handle windows messages for a class instance
	/// </summary>
	/// <param name="hWnd">window message is for</param>
	/// <param name="uMsg">message</param>
	/// <param name="wParam">message data</param>
	/// <param name="lParam">additional message data</param>
	/// <returns>result of message processing</returns>
	LRESULT CALLBACK        DlgProcSp(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	/// <summary>
	/// Creates the main window and begins processing
	/// </summary>
	/// <param name="hInstance">handle to the application instance</param>
	/// <param name="nCmdShow">whether to display minimized, maximized, or normally</param>
	int                     RunSp(HINSTANCE hInstance, int nCmdShow);

private:
	static LPCWSTR          GrammarFileName;

	// Main application dialog window
	HWND                    m_hWnd;


	// Object that controls moving turtle around and displaying it
	// TurtleController*       m_pTurtleController;

	// Current Kinect sensor
	IKinectSensor*          m_pKinectSensor;

	// A single audio beam off the Kinect sensor.
	IAudioBeam*             m_pAudioBeam;

	// An IStream derived from the audio beam, used to read audio samples
	IStream*                m_pAudioStream;

	// Stream for converting 32bit Audio provided by Kinect to 16bit required by speeck
	KinectAudioStream*     m_p16BitAudioStream;

	// Handle for sensor notifications
	WAITABLE_HANDLE         m_hSensorNotification;

	// Stream given to speech recognition engine
	ISpStream*              m_pSpeechStream;

	// Speech recognizer
	ISpRecognizer*          m_pSpeechRecognizer;

	// Speech recognizer context
	ISpRecoContext*         m_pSpeechContext;

	// Speech grammar
	ISpRecoGrammar*         m_pSpeechGrammar;

	// Event triggered when we detect speech recognition
	HANDLE                  m_hSpeechEvent;

	INT64                   m_nStartTime;
	INT64                   m_nLastCounter;
	double                  m_fFreq;
	INT64                   m_nNextStatusTime;
	DWORD                   m_nFramesSinceUpdate;
	//----------------------------------------------------------
	bool					m_bSaveScreenshot;
	//----------------------------------------------------------

	// Current Kinect
	ICoordinateMapper*      m_pCoordinateMapper;

	// Body reader
	IBodyFrameReader*       m_pBodyFrameReader;

	//----------------------------------------------------------
	// Depth reader
	IDepthFrameReader*		m_pDepthFrameReader;
	//----------------------------------------------------------

	// Direct2D
	ID2D1Factory*           m_pD2DFactory;
	//----------------------------------------------------------
	ImageRenderer*			m_pDrawDepth;
	RGBQUAD*				m_pDepthRGBX;
	//----------------------------------------------------------

	// Body/hand drawing
	ID2D1HwndRenderTarget*  m_pRenderTarget;
	ID2D1SolidColorBrush*   m_pBrushJointTracked;
	ID2D1SolidColorBrush*   m_pBrushJointInferred;
	ID2D1SolidColorBrush*   m_pBrushBoneTracked;
	ID2D1SolidColorBrush*   m_pBrushBoneInferred;
	ID2D1SolidColorBrush*   m_pBrushHandClosed;
	ID2D1SolidColorBrush*   m_pBrushHandOpen;
	ID2D1SolidColorBrush*   m_pBrushHandLasso;

	/// <summary>
	/// Opens the Kinect Sensor and Initialize Audio
	/// </summary>
	/// <returns>S_OK on success, otherwise failure code.</returns>
	HRESULT                 StartKinect();

	/// <summary>
	/// Initializes Speech
	/// </summary>
	/// <returns>S_OK on success, otherwise failure code.</returns>
	HRESULT                 InitializeSpeech();

	/// <summary>
	/// Create speech recognizer that will read Kinect audio stream data.
	/// </summary>
	/// <returns>
	/// <para>S_OK on success, otherwise failure code.</para>
	/// </returns>
	HRESULT                 CreateSpeechRecognizer();

	/// <summary>
	/// Load speech recognition grammar into recognizer.
	/// </summary>
	/// <returns>
	/// <para>S_OK on success, otherwise failure code.</para>
	/// </returns>
	HRESULT                 LoadSpeechGrammar();

	/// <summary>
	/// Start recognizing speech asynchronously.
	/// </summary>
	/// <returns>
	/// <para>S_OK on success, otherwise failure code.</para>
	/// </returns>
	HRESULT                 StartSpeechRecognition();

	/// <summary>
	/// Process recently triggered speech recognition events.
	/// </summary>
	void                    ProcessSpeech();

	/// <summary>
	/// Maps a specified speech semantic tag to the corresponding action to be performed on turtle.
	/// </summary>
	/// <returns>
	/// Action that matches <paramref name="pszSpeechTag"/>, or TurtleActionNone if no matches were found.
	/// </returns>
	int			            MapSpeechTagToAction(LPCWSTR pszSpeechTag);

	/// <summary>
	/// Main processing function
	/// </summary>
	void                    Update();

	/// <summary>
	/// Initializes the default Kinect sensor
	/// </summary>
	/// <returns>S_OK on success, otherwise failure code</returns>
	HRESULT                 InitializeDefaultSensor();

	/// <summary>
	/// Handle new body data
	/// <param name="nTime">timestamp of frame</param>
	/// <param name="nBodyCount">body data count</param>
	/// <param name="ppBodies">body data in frame</param>
	/// </summary>
	void                    ProcessBody(INT64 nTime, int nBodyCount, IBody** ppBodies);

	/// <summary>
	/// Set the status bar message
	/// </summary>
	/// <param name="szMessage">message to display</param>
	/// <param name="nShowTimeMsec">time in milliseconds for which to ignore future status messages</param>
	/// <param name="bForce">force status update</param>
	bool                    SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce);

	/// <summary>
	/// Ensure necessary Direct2d resources are created
	/// </summary>
	/// <returns>S_OK if successful, otherwise an error code</returns>
	HRESULT EnsureDirect2DResources();

	/// <summary>
	/// Dispose Direct2d resources 
	/// </summary>
	void DiscardDirect2DResources();

	/// <summary>
	/// Converts a body point to screen space
	/// </summary>
	/// <param name="bodyPoint">body point to tranform</param>
	/// <param name="width">width (in pixels) of output buffer</param>
	/// <param name="height">height (in pixels) of output buffer</param>
	/// <returns>point in screen-space</returns>
	D2D1_POINT_2F           BodyToScreen(const CameraSpacePoint& bodyPoint, int width, int height);

	/// <summary>
	/// Draws a body 
	/// </summary>
	/// <param name="pJoints">joint data</param>
	/// <param name="pJointPoints">joint positions converted to screen space</param>
	void                    DrawBody(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints);

	/// <summary>
	/// Draws a hand symbol if the hand is tracked: red circle = closed, green circle = opened; blue circle = lasso
	/// </summary>
	/// <param name="handState">state of the hand</param>
	/// <param name="handPosition">position of the hand</param>
	void                    DrawHand(HandState handState, const D2D1_POINT_2F& handPosition);

	/// <summary>
	/// Draws one bone of a body (joint to joint)
	/// </summary>
	/// <param name="pJoints">joint data</param>
	/// <param name="pJointPoints">joint positions converted to screen space</param>
	/// <param name="pJointPoints">joint positions converted to screen space</param>
	/// <param name="joint0">one joint of the bone to draw</param>
	/// <param name="joint1">other joint of the bone to draw</param>
	void                    DrawBone(const Joint* pJoints, const D2D1_POINT_2F* pJointPoints, JointType joint0, JointType joint1);
};
