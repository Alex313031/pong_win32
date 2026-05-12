// Win32 Pong Game
// Written by Govind Parmar
// Do not use Unicode or the score at the bottom will fail to update properly.

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
RECT humpad, comppad, ball;
RECT UpdatePaddle(int ulX, int ulY)
{
	RECT retVal;
	retVal.left = ulX;
	retVal.top = ulY;
	retVal.right = ulX + 35;
	retVal.bottom = ulY + 10;
	return retVal;
}

RECT UpdateBall(int ulX, int ulY)
{
	RECT retVal;
	retVal.left = ulX;
	retVal.top = ulY;
	retVal.right = ulX + 7;
	retVal.bottom = ulY + 7;
	return retVal;
}
int started = 0;
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{

	WNDCLASSEX wc;
	MSG Msg;
	HWND hWnd, hScore;
	HPEN objPen, bgPen;
	HDC pongDC;
	RECT winRect;
	HBRUSH objBrush, bgBrush;
	int yDirection = 1; //0 = up, 1 = down
	int xDirection = 0; // 0 = straight, 1/-1 = more vertical, 2/-2 full diagonal, 3/-3 more horizantal
	int scoreH = 0, scoreC = 0;
	if (FindWindow("PongWndClass", "Pong"))
	{
		MessageBox(0, "Pong is already running! Close pong or end pong.exe in task manager if it is not responding.", "Problem", MB_OK | MB_ICONEXCLAMATION);
		return 0;
	}
	wc.cbClsExtra = 0;
	wc.cbSize = sizeof(WNDCLASSEX);
	wc.cbWndExtra = 0;
	wc.hbrBackground = CreateSolidBrush(RGB(0x00, 0x00, 0x00));
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
	wc.hInstance = hInstance;
	wc.lpfnWndProc = WndProc;
	wc.lpszClassName = "PongWndClass";
	wc.lpszMenuName = NULL;
	wc.style = CS_OWNDC;
	RegisterClassEx(&wc);
	hWnd = CreateWindowEx(WS_EX_OVERLAPPEDWINDOW, "PongWndClass", "Pong", WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX, 100, 100, 300, 370, NULL, NULL, hInstance, NULL);
	hScore = CreateWindowEx(0, "STATIC", "Press spacebar to start.", WS_VISIBLE | WS_CHILD, 0, 300, 300, 50, hWnd, NULL, hInstance, NULL);
	ShowWindow(hWnd, SW_SHOW);
	pongDC = GetDC(hWnd);
	GetWindowRect(hWnd, &winRect);
	objPen = CreatePen(PS_SOLID, 1, RGB(0x00, 0xFF, 0x00));
	bgPen = CreatePen(PS_SOLID, 1, RGB(0x00, 0x00, 0x00));
	bgBrush = CreateSolidBrush(RGB(0x00, 0x00, 0x00));
	objBrush = CreateSolidBrush(RGB(0x00, 0xFF, 0x00));
	ball = UpdateBall(125, 240);
	humpad = UpdatePaddle(125, 250);
	comppad = UpdatePaddle(125, 1);
	UpdateWindow(hWnd);
	FillRect(pongDC, &humpad, CreateSolidBrush(RGB(0x00, 0xFF, 0x00)));
	FillRect(pongDC, &comppad, CreateSolidBrush(RGB(0x00, 0xFF, 0x00)));
	SelectObject(pongDC, objPen);
	Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
	PeekMessage(&Msg, NULL, 0, 0, PM_NOREMOVE);
	while (Msg.message != WM_QUIT)
	{
		if (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}
		else
		{
			TCHAR scoreTxt[1000];
			if (scoreH == 10)
			{
				MessageBox(0, "Congratulations, you won!", "Game over", MB_OK | MB_ICONINFORMATION);
				scoreH = 0;
				scoreC = 0;
				started = 0;
				SetWindowText(hScore, "You win!");
			}
			if (scoreC == 10)
			{
				MessageBox(0, "Sorry, you lost!", "Game over", MB_OK | MB_ICONINFORMATION);
				scoreH = 0;
				scoreC = 0;
				started = 0;
				SetWindowText(hScore, "Computer wins!");
			}
			if (started)
			{

				sprintf(scoreTxt, "Human: %d\tComputer: %d", scoreH, scoreC);
				SetWindowText(hScore, scoreTxt);
				if (comppad.left > ball.right && comppad.left >= 10)
				{
					int amt;
					srand(GetTickCount());
					amt = rand()%9;
					FillRect(pongDC, &comppad, bgBrush);
					comppad = UpdatePaddle(comppad.left - amt, comppad.top);
					FillRect(pongDC, &comppad, objBrush);
				}
				if (comppad.right < ball.left && comppad.right <= 285)
				{
					int amt;
					srand(GetTickCount());
					amt = rand()%9;
					FillRect(pongDC, &comppad, bgBrush);
					comppad = UpdatePaddle(comppad.left + amt, comppad.top);
					FillRect(pongDC, &comppad, objBrush);
				}
				if (ball.left <= 15 || ball.right >= 285)
				{
					xDirection = -1 * xDirection;
				}
				if (yDirection == 1)
				{
					if (ball.bottom <= 243)
					{
						SelectObject(pongDC, bgPen);
						SelectObject(pongDC, bgBrush);
						Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
						UpdateWindow(hWnd);

						SelectObject(pongDC, objPen);
						SelectObject(pongDC, objBrush);
						ball = UpdateBall(ball.left + xDirection, ball.top + 5);
						Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
						UpdateWindow(hWnd);
					}
					else if (ball.bottom > 240)
					{
						if ((ball.right + 7 < humpad.left) || (ball.left > 7 + humpad.right)) // computer scores
						{
							SelectObject(pongDC, bgPen);
							SelectObject(pongDC, bgBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							UpdateWindow(hWnd);

							SelectObject(pongDC, objPen);
							SelectObject(pongDC, objBrush);
							ball = UpdateBall(ball.left + xDirection, ball.top + 5);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							UpdateWindow(hWnd);
							scoreC++;
							FillRect(pongDC, &humpad, bgBrush);
							humpad = UpdatePaddle(125, 250);
							FillRect(pongDC, &humpad, objBrush);
							FillRect(pongDC, &comppad, bgBrush);
							comppad = UpdatePaddle(125, 1);
							FillRect(pongDC, &comppad, objBrush);
							SelectObject(pongDC, bgPen);
							SelectObject(pongDC, bgBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							ball = UpdateBall(125, 240);

							SelectObject(pongDC, objPen);
							SelectObject(pongDC, objBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							sprintf(scoreTxt, "Human: %d\tComputer: %d", scoreH, scoreC);
							SetWindowText(hScore, scoreTxt);
							started = 0;
						}
						else // ball deflected
						{
							if (ball.right - humpad.left > 0)
							{
								if (ball.right - humpad.left <= 5)
								{
									xDirection = -3;
								}
								else if (ball.right - humpad.left <= 10 && ball.right - humpad.left > 5)
								{
									xDirection = -2;
								}
								else if (ball.right - humpad.left <= 15 && ball.right - humpad.left > 10)
								{
									xDirection = -1;
								}
								else if (ball.right - humpad.left <= 25 && ball.right - humpad.left > 20)
								{
									xDirection = 1;
								}
								else if (ball.right - humpad.left <= 30 && ball.right - humpad.left > 25)
								{
									xDirection = 2;
								}
								else if (ball.right - humpad.left >= 30)
								{
									xDirection = 3;
								}
								else
								{
									xDirection = 0;
								}
							}

							yDirection = 0;
						}
						//yDirection = 0;

					}
				}
				else if (yDirection == 0)
				{
					SelectObject(pongDC, bgPen);
					SelectObject(pongDC, bgBrush);
					Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
					UpdateWindow(hWnd);

					SelectObject(pongDC, objPen);
					SelectObject(pongDC, objBrush);
					ball = UpdateBall(ball.left + xDirection, ball.top - 5);
					Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
					UpdateWindow(hWnd);
					if (ball.top <= 10)
					{
						if ((ball.right + 5 < comppad.left) || (ball.left > comppad.right + 5))
						{
							SelectObject(pongDC, bgPen);
							SelectObject(pongDC, bgBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							UpdateWindow(hWnd);

							SelectObject(pongDC, objPen);
							SelectObject(pongDC, objBrush);

							ball = UpdateBall(ball.left + xDirection, ball.top - 5);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							UpdateWindow(hWnd);
							scoreH++;
							FillRect(pongDC, &humpad, bgBrush);
							humpad = UpdatePaddle(125, 250);
							FillRect(pongDC, &humpad, objBrush);
							FillRect(pongDC, &comppad, bgBrush);
							comppad = UpdatePaddle(125, 1);
							FillRect(pongDC, &comppad, objBrush);
							SelectObject(pongDC, bgPen);
							SelectObject(pongDC, bgBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							ball = UpdateBall(125, 240);

							SelectObject(pongDC, objPen);
							SelectObject(pongDC, objBrush);
							Ellipse(pongDC, ball.left, ball.top, ball.right, ball.bottom);
							sprintf(scoreTxt, "Human: %d\tComputer: %d", scoreH, scoreC);
							SetWindowText(hScore, scoreTxt);
							started = 0;
						}
						else
						{
							if (ball.right - comppad.left > 0)
							{
								if (ball.right - comppad.left <= 5)
								{
									xDirection = -3;
								}
								else if (ball.right - comppad.left <= 10 && ball.right - comppad.left > 5)
								{
									xDirection = -2;
								}
								else if (ball.right - comppad.left <= 15 && ball.right - comppad.left > 10)
								{
									xDirection = -1;
								}
								else if (ball.right - comppad.left <= 25 && ball.right - comppad.left > 20)
								{
									xDirection = 1;
								}
								else if (ball.right - comppad.left <= 30 && ball.right - comppad.left > 25)
								{
									xDirection = 2;
								}
								else if (ball.right - comppad.left >= 35)
								{
									xDirection = 3;
								}
								else
								{
									xDirection = 0;
								}
							}
							yDirection = 1;
						}
					}
				}
			}
			Sleep(30);
		}
	}
	return Msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HDC pongDC = GetDC(hWnd);
	HPEN objPen = CreatePen(PS_SOLID, 1, 0x0000FF00);
	switch (Msg)
	{
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_KEYDOWN:
		switch ((int)wParam)
		{
		case VK_LEFT:
		{
						if (started)
						{
							SelectObject(pongDC, objPen);
							if (humpad.left - 10 <= 0)
							{
								break;
							}
							FillRect(pongDC, &humpad, CreateSolidBrush(RGB(0x00, 0x00, 0x00)));

							humpad = UpdatePaddle(humpad.left - 8, humpad.top);
							FillRect(pongDC, &humpad, CreateSolidBrush(RGB(0x00, 0xFF, 0x00)));
						}
		}
			break;
		case VK_RIGHT:
		{
						 if (started)
						 {
							 SelectObject(pongDC, objPen);
							 if (humpad.left + 10 >= 250)
							 {
								 break;
							 }
							 FillRect(pongDC, &humpad, CreateSolidBrush(RGB(0x00, 0x00, 0x00)));

							 humpad = UpdatePaddle(humpad.left + 8, humpad.top);
							 FillRect(pongDC, &humpad, CreateSolidBrush(RGB(0x00, 0xFF, 0x00)));
						 }
		}
			break;
		case VK_SPACE:
		{
						 if (!started)
						 {
							 started = 1;
						 }

		}
			break;
		default:
			RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE);
			return DefWindowProc(hWnd, WM_KEYDOWN, wParam, lParam);
		}
	default:
		return DefWindowProc(hWnd, Msg, wParam, lParam);
		break;
	}
	return 0;
}
