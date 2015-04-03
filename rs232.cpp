#include "rs232.h"


RS232::RS232(int comNum, int baudrate, int bytesize, int parity, int stopbit){


	/***********************************************************************
	//Find the com port that has been assigned to your device.
	/***********************************************************************/
	/*
	res = FT_Open(0, &fthandle);

	if (res != FT_OK){

	printf("opening failed! with error %d\n", res);

	return;
	}


	res = FT_GetComPortNumber(fthandle, &COMPORT);

	if (res != FT_OK){

	printf("get com port failed %d\n", res);

	return;
	}

	if (COMPORT == -1){

	printf("no com port installed \n");
	}

	else{
	printf("com port number is %d\n", COMPORT);

	}


	FT_Close(fthandle);
	*/

	/********************************************************/
	// Open the com port assigned to your device
	/********************************************************/

	// n = sprintf_s(COMx, "\\\\.\\COM%d", COMPORT);

	// myMutex = CreateMutex(NULL, TRUE, NULL);

	InitializeCriticalSection(&cs);
	memset(gStr, 0, 256);

	n = sprintf_s(COMx, "\\\\.\\COM%d", comNum);

	hCommPort = CreateFileA(
		COMx,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
		);

	if (hCommPort == INVALID_HANDLE_VALUE)
	{

		printf("Help - failed to open\n");
		return;

	}


	printf("Hello World!\n");

	/********************************************************/
	// Configure the UART interface parameters
	/********************************************************/

	fSuccess = GetCommState(hCommPort, &dcb);


	if (!fSuccess)

	{
		printf("GetCommStateFailed \n", GetLastError());
		return;

	}

	//set parameters.

	dcb.BaudRate = baudrate;
	dcb.ByteSize = bytesize;
	dcb.Parity = parity;
	dcb.StopBits = stopbit;

	fSuccess = SetCommState(hCommPort, &dcb);


	if (!fSuccess)

	{
		printf("SetCommStateFailed \n", GetLastError());
		return;

	}


	printf("Port configured \n");

	myHThread = CreateThread(NULL, 0,
		ThreadFunc, (LPVOID)this,
		CREATE_SUSPENDED, &myThreadId);
}

RS232::~RS232(){
	/********************************************************/
	//Closing the device at the end of the program
	/********************************************************/
	CloseHandle(hCommPort);
	CloseHandle(myHThread);

	DeleteCriticalSection(&cs);

	return;
}

DWORD WINAPI RS232::ThreadFunc(LPVOID lpParameter)
{

	// while (true){
	// 	((CSerialPortProcessor*)lpParameter)->ReceiveData();
	// }
	while (true){
		((RS232*)lpParameter)->read();
	}

	// return ((RS232*)lpParameter)->ReceiveData();
	return S_OK;
}

void RS232::StartThread(){
	ResumeThread(myHThread);
}

void RS232::write(char* data_out){
	/********************************************************/
	// Writing data to the USB to UART converter
	/********************************************************/

	DWORD dwwritten = 0, dwErr;
	DWORD w_data_len = strlen(data_out);

	while (reading == 1);
	fSuccess = WriteFile(hCommPort, data_out, w_data_len, &dwwritten, NULL);
	Sleep(300);

	printf("%s\n", data_out);
	if (!fSuccess)

	{
		dwErr = GetLastError();
		printf("Write Failed \n", GetLastError());
		return;

	}


	printf("bytes written = %d\n", dwwritten);
}

void RS232::read(){
	/********************************************************/
	//Reading data from the USB to UART converter
	/********************************************************/
	char buf[256];
	DWORD dwRead;
	DWORD dwErrors;
	COMSTAT ComStat;
	DWORD r_data_len = 0;	// この数の文字を受け取るまで待機

	int count = 0;

	memset(buf, 0, 256);

	ClearCommError(hCommPort, &dwErrors, &ComStat);
	r_data_len = ComStat.cbInQue;
	if (r_data_len != 0){
		reading = 1;
		EnterCriticalSection(&cs);
		memset(gStr, 0, 256);
		while (r_data_len != 0){
			count++;
			if (ReadFile(hCommPort, buf, r_data_len, &dwRead, NULL))
			{
				strcat(gStr, buf);
				memset(buf, 0, 256);

			}
			else{
				printf("Read Failed \n");
			}

			Sleep(6);	//	後で計算してみよう(ボーレートから計算できると思ったけど計算しようがない，シリアルをオシロで見たら分かるとは思う)

			ClearCommError(hCommPort, &dwErrors, &ComStat);
			r_data_len = ComStat.cbInQue;
		}
		LeaveCriticalSection(&cs);
		reading = 0;
	}
}
/*
void RS232::read(){

	DWORD dwRead;
	DWORD dwErrors;
	COMSTAT ComStat;
	DWORD r_data_len = 0;	// この数の文字を受け取るまで待機


	memset(gBuf, 0, 256);

		ClearCommError(hCommPort, &dwErrors, &ComStat);
		r_data_len = ComStat.cbInQue;
		if (r_data_len != 0){
			EnterCriticalSection(&cs);
			if (ReadFile(hCommPort, gBuf, r_data_len, &dwRead, NULL))

			{

				// printf("data read = %s\n", buf);
				printf("%s\n\n\n\n\n", gBuf);	// 現状バッファにたまってるデータ列を全部取り出してる．結局受信データはタイミングによって途切れてる．

			}
			else{
				printf("Read Failed \n");
			}
			LeaveCriticalSection(&cs);
		}
}
*/