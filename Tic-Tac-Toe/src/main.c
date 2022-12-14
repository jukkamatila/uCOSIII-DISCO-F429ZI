/*
*********************************************************************************************************
*                                            LOCAL INCLUDES
*********************************************************************************************************
*/

#include "main.h"

/*
*********************************************************************************************************
*                                            LOCAL DEFINES
*********************************************************************************************************
*/

/* Task Stack Size */
#define TASK_STK_SIZE 256u

/* Task Priority */
#define APP_TASK_START_PRIO 1u
#define DRAW_BOARD_PRIO 2u
#define BOT_PLAYER_PRIO 3u
#define HUMAN_PLAYER_PRIO 3u
#define ANALYSIS_PRIO 4u

/* Debug logger */
// typedef unsigned char LOG_MASK    //typedef not working
#define LOG_TOUCH_SCREEN (uint8_t)0x01
#define LOG_BOARD_DATA_CORRUPT (uint8_t)0x02
#define LOG_CPU_STATUS (uint8_t)0x20
#define LOG_TASK_COUNT (uint8_t)0x40
#define LOG_CONTEXT_SWITCH (uint8_t)0x80
#define LOG_OS_STATUS (uint8_t)0xe0
#define LOG_ALL (uint8_t)0xff
#define LOG_EN 1u

/* Board parameters */
#define BOARD_SIZE 9
#define CIRCLE_RADIUS (uint16_t)20
#define CROSS_SIZE (uint16_t)20

/* Data structures */
typedef struct tuples
{
    uint16_t X;
    uint16_t Y;
} tuple;

/*
*********************************************************************************************************
*                                           GLOBAL VARIABLES
*********************************************************************************************************
*/

/* Task Control Block */
static OS_TCB AppTaskStartTCB;
static OS_TCB DrawBoardTCB;
static OS_TCB BotPlayerTCB;
static OS_TCB HumanPlayerTCB;
static OS_TCB AnalysisTCB;

/* Task Stack */
static CPU_STK AppTaskStartStk[TASK_STK_SIZE];
static CPU_STK DrawBoardStk[TASK_STK_SIZE];
static CPU_STK BotPlayerStk[TASK_STK_SIZE];
static CPU_STK HumanPlayerStk[TASK_STK_SIZE];
static CPU_STK AnalysisStk[TASK_STK_SIZE];

static TS_StateTypeDef TS_State;

CPU_INT08U board[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0}; // 0 is empty, 1 is bot player, 2 is human player
CPU_INT08U moves = 0;
tuple gridCenterCoordinates[9] = {{40, 280}, {120, 280}, {200, 280}, {40, 200}, {120, 200}, {200, 200}, {40, 120}, {120, 120}, {200, 120}};
OS_MUTEX mutex;

/*
*********************************************************************************************************
*                                         FUNCTION PROTOTYPES
*********************************************************************************************************
*/

/* Task Prototypes */
static void AppTaskStart(void *p_arg);
static void DrawBoard(void *p_arg);
static void BotPlayer(void *p_arg);
static void HumanPlayer(void *p_arg);
static void Analysis(void *p_arg);

/* System Initilization Prototypes */
static void SystemClock_Config(void);
static void LCD_Init(void);
static void PrintResult(uint8_t who);
static void GameOver(void);
static void drawCross(const uint16_t x, const uint16_t y, const uint16_t size);
static void drawMark();
static void logger(const uint8_t mask);
static const CPU_INT08S touchInput();

/*
*********************************************************************************************************
*                                                MAIN
*********************************************************************************************************
*/

int main(void)
{
    OS_ERR err;

    OSInit(&err);

    OSMutexCreate((OS_MUTEX *)&mutex,
                  (CPU_CHAR *)"Resource Mutex",
                  (OS_ERR *)&err);

    OSTaskCreate((OS_TCB *)&AppTaskStartTCB,
                 (CPU_CHAR *)"App Task Start",
                 (OS_TASK_PTR)AppTaskStart,
                 (void *)0,
                 (OS_PRIO)APP_TASK_START_PRIO,
                 (CPU_STK *)&AppTaskStartStk[0],
                 (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                 (CPU_STK_SIZE)TASK_STK_SIZE,
                 (OS_MSG_QTY)5u,
                 (OS_TICK)0u,
                 (void *)0,
                 (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                 (OS_ERR *)&err);

    OSStart(&err);
}

/*
*********************************************************************************************************
*                                              STARTUP TASK
*********************************************************************************************************
*/

static void AppTaskStart(void *p_arg)
{
    OS_ERR err;

    HAL_Init();

    SystemClock_Config();

    BSP_LED_Init(LED3);
    BSP_LED_Init(LED4);

    LCD_Init();

    uint8_t status = 0;
    status = BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize());

    if (status != TS_OK)
    {
        BSP_LCD_SetBackColor(LCD_COLOR_WHITE);
        BSP_LCD_SetTextColor(LCD_COLOR_RED);
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 95, (uint8_t *)"ERROR", CENTER_MODE);
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 80, (uint8_t *)"Touchscreen cannot be initialized", CENTER_MODE);
    }
    else
    {
        OSTaskCreate((OS_TCB *)&DrawBoardTCB,
                     (CPU_CHAR *)"Draw Board Task",
                     (OS_TASK_PTR)DrawBoard,
                     (void *)0,
                     (OS_PRIO)DRAW_BOARD_PRIO,
                     (CPU_STK *)&DrawBoardStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskSemPost((OS_TCB *)&DrawBoardTCB, // Notify receive task to send next message
                      (OS_OPT)OS_OPT_POST_NONE,
                      (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&BotPlayerTCB,
                     (CPU_CHAR *)"Bot Player Task",
                     (OS_TASK_PTR)BotPlayer,
                     (void *)0,
                     (OS_PRIO)BOT_PLAYER_PRIO,
                     (CPU_STK *)&BotPlayerStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&HumanPlayerTCB,
                     (CPU_CHAR *)"Human Player Task",
                     (OS_TASK_PTR)HumanPlayer,
                     (void *)0,
                     (OS_PRIO)HUMAN_PLAYER_PRIO,
                     (CPU_STK *)&HumanPlayerStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&AnalysisTCB,
                     (CPU_CHAR *)"Analysis Task",
                     (OS_TASK_PTR)Analysis,
                     (void *)0,
                     (OS_PRIO)ANALYSIS_PRIO,
                     (CPU_STK *)&AnalysisStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);
    }
}

/*
*********************************************************************************************************
*                                                  TASKS
*********************************************************************************************************
*/

static void DrawBoard(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    uint8_t turn = 1;

    BSP_LCD_Clear(LCD_COLOR_BLACK);
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_DrawHLine(0, BSP_LCD_GetYSize() - 240, 240);
    BSP_LCD_DrawHLine(0, BSP_LCD_GetYSize() - 160, 240);
    BSP_LCD_DrawHLine(0, BSP_LCD_GetYSize() - 80, 240);
    BSP_LCD_DrawVLine(BSP_LCD_GetXSize() - 160, 80, 240);
    BSP_LCD_DrawVLine(BSP_LCD_GetXSize() - 80, 80, 240);

    while (DEF_TRUE)
    {
        OSTaskSemPend((OS_TICK)0, // Wait for a notification to send next message
                      (OS_OPT)OS_OPT_PEND_BLOCKING,
                      (CPU_TS *)&ts,
                      (OS_ERR *)&err);

        drawMark();

        switch (turn)
        {
        case 1:
            OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
                          (OS_OPT)OS_OPT_POST_NONE,
                          (OS_ERR *)&err);
            break;
        case 2:
            OSTaskSemPost((OS_TCB *)&HumanPlayerTCB,
                          (OS_OPT)OS_OPT_POST_NONE,
                          (OS_ERR *)&err);
        default:
            break;
        }
        turn++;
        if (turn >= 3)
        {
            turn = 1;
        }
    }
}

// TO-DO seperate the data operation from UI operation
static void BotPlayer(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    CPU_INT08U move;

    while (DEF_TRUE)
    {
        OSTaskSemPend((OS_TICK)0, // Wait for a notification to send next message
                      (OS_OPT)OS_OPT_PEND_BLOCKING,
                      (CPU_TS *)&ts,
                      (OS_ERR *)&err);

        OSMutexPend((OS_MUTEX *)&mutex,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        BSP_LCD_SetTextColor(LCD_COLOR_GREEN);

        srand(OSTickCtr);
        move = rand() % 9;

        while (board[move] > 0)
        {
            move = rand() % 9;
        }
        /*
                switch (move)
                {
                case 0:
                    BSP_LCD_DrawCircle(40, 120, 20);
                    break;
                case 1:
                    BSP_LCD_DrawCircle(120, 120, 20);
                    break;
                case 2:
                    BSP_LCD_DrawCircle(200, 120, 20);
                    break;
                case 3:
                    BSP_LCD_DrawCircle(40, 200, 20);
                    break;
                case 4:
                    BSP_LCD_DrawCircle(120, 200, 20);
                    break;
                case 5:
                    BSP_LCD_DrawCircle(200, 200, 20);
                    break;
                case 6:
                    BSP_LCD_DrawCircle(40, 280, 20);
                    break;
                case 7:
                    BSP_LCD_DrawCircle(120, 280, 20);
                    break;
                case 8:
                    BSP_LCD_DrawCircle(200, 280, 20);
                    break;
                }
        */
        board[move] = 1;

        moves++;

        OSTimeDlyHMSM((CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT32U)100u,
                      (OS_OPT)OS_OPT_TIME_HMSM_STRICT,
                      (OS_ERR *)&err);

        OSMutexPost((OS_MUTEX *)&mutex,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSTaskSemPost((OS_TCB *)&DrawBoardTCB,
                      (OS_OPT)OS_OPT_POST_NONE,
                      (OS_ERR *)&err);
    }
}

static void HumanPlayer(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    CPU_INT08S index;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        BSP_TS_GetState(&TS_State);
        BSP_LCD_SetTextColor(LCD_COLOR_RED);

#if LOG_EN > 0
        logger(LOG_TOUCH_SCREEN);
#endif

        //------------------------------------------------------------------
        //! TS_State coordinates origin (0,0) is at the left BOTTOM corner
        //! BSP_LCD_XXX() coordinates origin (0,0) is at the left TOP corner
        //! The original auther had the Y coodrinates mis-calculated,
        //! (BSP_LCD_GetYSize() - TS_State.Y) is used to fix Y coordinates
        //! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
        //------------------------------------------------------------------
        // TO-DO new user input mapping algorithm
        // original: best case = 8 comparisms, worst scenario = 8 * 9 = 72 comparisms
        // new algorithm: best case = 4 comparisms, worst scenario = 6 comparisms
        index = touchInput();
        if (index >= 0)
        {
            if(board[index] == 0)
            {
                board[index] = 2;
                moves++;
            }
            OSTaskSemPost((OS_TCB *)&DrawBoardTCB,
                          (OS_OPT)OS_OPT_POST_NONE,
                          (OS_ERR *)&err);
        }
        // if (board[0] == 0 && TS_State.TouchDetected && TS_State.X < 80 && (BSP_LCD_GetYSize() - TS_State.Y) > 80 && (BSP_LCD_GetYSize() - TS_State.Y) < 160)
        // {
        //     BSP_LCD_DrawLine(20, 100, 60, 140);
        //     BSP_LCD_DrawLine(60, 100, 20, 140);
        //     board[0] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[1] == 0 && TS_State.TouchDetected && TS_State.X > 80 && TS_State.X < 160 && (BSP_LCD_GetYSize() - TS_State.Y) > 80 && (BSP_LCD_GetYSize() - TS_State.Y) < 160)
        // {
        //     BSP_LCD_DrawLine(100, 100, 140, 140);
        //     BSP_LCD_DrawLine(140, 100, 100, 140);
        //     board[1] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[2] == 0 && TS_State.TouchDetected && TS_State.X > 160 && TS_State.X < 240 && (BSP_LCD_GetYSize() - TS_State.Y) > 80 && (BSP_LCD_GetYSize() - TS_State.Y) < 160)
        // {
        //     BSP_LCD_DrawLine(180, 100, 220, 140);
        //     BSP_LCD_DrawLine(220, 100, 180, 140);
        //     board[2] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[3] == 0 && TS_State.TouchDetected && TS_State.X < 80 && (BSP_LCD_GetYSize() - TS_State.Y) > 160 && (BSP_LCD_GetYSize() - TS_State.Y) < 240)
        // {
        //     BSP_LCD_DrawLine(20, 180, 60, 220);
        //     BSP_LCD_DrawLine(60, 180, 20, 220);
        //     board[3] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[4] == 0 && TS_State.TouchDetected && TS_State.X > 80 && TS_State.X < 160 && (BSP_LCD_GetYSize() - TS_State.Y) > 160 && (BSP_LCD_GetYSize() - TS_State.Y) < 240)
        // {
        //     BSP_LCD_DrawLine(100, 180, 140, 220);
        //     BSP_LCD_DrawLine(140, 180, 100, 220);
        //     board[4] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[5] == 0 && TS_State.TouchDetected && TS_State.X > 160 && TS_State.X < 240 && (BSP_LCD_GetYSize() - TS_State.Y) > 160 && (BSP_LCD_GetYSize() - TS_State.Y) < 240)
        // {
        //     BSP_LCD_DrawLine(180, 180, 220, 220);
        //     BSP_LCD_DrawLine(220, 180, 180, 220);
        //     board[5] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[6] == 0 && TS_State.TouchDetected && TS_State.X < 80 && (BSP_LCD_GetYSize() - TS_State.Y) > 80 && (BSP_LCD_GetYSize() - TS_State.Y) > 240 && (BSP_LCD_GetYSize() - TS_State.Y) < BSP_LCD_GetYSize())
        // {
        //     BSP_LCD_DrawLine(20, 260, 60, 300);
        //     BSP_LCD_DrawLine(60, 260, 20, 300);
        //     board[6] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[7] == 0 && TS_State.TouchDetected && TS_State.X > 80 && TS_State.X < 160 && (BSP_LCD_GetYSize() - TS_State.Y) > 240 && (BSP_LCD_GetYSize() - TS_State.Y) < BSP_LCD_GetYSize())
        // {
        //     BSP_LCD_DrawLine(100, 260, 140, 300);
        //     BSP_LCD_DrawLine(140, 260, 100, 300);
        //     board[7] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }
        // else if (board[8] == 0 && TS_State.TouchDetected && TS_State.X > 160 && TS_State.X < 240 && (BSP_LCD_GetYSize() - TS_State.Y) > 240 && (BSP_LCD_GetYSize() - TS_State.Y) < BSP_LCD_GetYSize())
        // {
        //     BSP_LCD_DrawLine(180, 260, 220, 300);
        //     BSP_LCD_DrawLine(220, 260, 180, 300);
        //     board[8] = 2;
        //     moves++;
        //     OSTaskSemPost((OS_TCB *)&BotPlayerTCB,
        //                   (OS_OPT)OS_OPT_POST_NONE,
        //                   (OS_ERR *)&err);
        // }

        OSTimeDlyHMSM((CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT32U)100u,
                      (OS_OPT)OS_OPT_TIME_HMSM_STRICT,
                      (OS_ERR *)&err);

        OSMutexPost((OS_MUTEX *)&mutex,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);
    }
}

static void Analysis(void *p_arg)
{
    OS_ERR err;

    while (DEF_TRUE)
    {
        // TO-DO new analys algorithm
        //  currently 5 * 17 + 1 = 86 comparisms
        if (board[0] == 1 && board[1] == 1 && board[2] == 1)
        {
            PrintResult(1);
        }
        else if (board[3] == 1 && board[4] == 1 && board[5] == 1)
        {
            PrintResult(1);
        }
        else if (board[6] == 1 && board[7] == 1 && board[8] == 1)
        {
            PrintResult(1);
        }
        else if (board[0] == 1 && board[3] == 1 && board[6] == 1)
        {
            PrintResult(1);
        }
        else if (board[1] == 1 && board[4] == 1 && board[7] == 1)
        {
            PrintResult(1);
        }
        else if (board[2] == 1 && board[5] == 1 && board[8] == 1)
        {
            PrintResult(1);
        }
        else if (board[0] == 1 && board[4] == 1 && board[8] == 1)
        {
            PrintResult(1);
        }
        else if (board[2] == 1 && board[4] == 1 && board[6] == 1)
        {
            PrintResult(1);
        }
        else if (board[0] == 2 && board[1] == 2 && board[2] == 2)
        {
            PrintResult(2);
        }
        else if (board[3] == 2 && board[4] == 2 && board[5] == 2)
        {
            PrintResult(2);
        }
        else if (board[6] == 2 && board[7] == 2 && board[8] == 2)
        {
            PrintResult(2);
        }
        else if (board[0] == 2 && board[3] == 2 && board[6] == 2)
        {
            PrintResult(2);
        }
        else if (board[1] == 2 && board[4] == 2 && board[7] == 2)
        {
            PrintResult(2);
        }
        else if (board[2] == 2 && board[5] == 2 && board[8] == 2)
        {
            PrintResult(2);
        }
        else if (board[0] == 2 && board[4] == 2 && board[8] == 2)
        {
            PrintResult(2);
        }
        else if (board[2] == 2 && board[4] == 2 && board[6] == 2)
        {
            PrintResult(2);
        }
        else if (moves == 9)
        {
            PrintResult(0);
        }

        OSTimeDlyHMSM((CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT16U)0,
                      (CPU_INT32U)100u,
                      (OS_OPT)OS_OPT_TIME_HMSM_STRICT,
                      (OS_ERR *)&err);
    }
}

/*
*********************************************************************************************************
*                                      NON-TASK FUNCTIONS
*********************************************************************************************************
*/
//----------------------------------------------------------
//! Draw mark on the game board
//! \param [IN] x - x-coordinate of the cross center
//! \param [IN] y - y-coordinate of the cross center
//! \param [IN] size - half-width of the cross in pixels
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
//----------------------------------------------------------
static void drawCross(const uint16_t x, const uint16_t y, const uint16_t size)
{
    BSP_LCD_DrawLine(x - size, y - size, x + size, y + size); // '\' of the cross
    BSP_LCD_DrawLine(x + size, y - size, x - size, y + size); // '/' of the cross
}

//----------------------------------------------------------
//! Draw mark on the game board
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
//----------------------------------------------------------
static void drawMark()
{
    for (uint8_t i = 0; i < BOARD_SIZE; i++)
    {
        switch (board[i])
        {
        case 0:
            // Do Nothing
            break;
        case 1:
            BSP_LCD_DrawCircle(gridCenterCoordinates[i].X, gridCenterCoordinates[i].Y, CIRCLE_RADIUS);
            break;
        case 2:
            drawCross(gridCenterCoordinates[i].X, gridCenterCoordinates[i].Y, CROSS_SIZE);
            break;
        default:
            logger(LOG_BOARD_DATA_CORRUPT);
            // TO-DO throuw some kind of exception
            break;
        }
    }
}

//----------------------------------------------------------
//! Converts user touch input to index for the board
//! \return [OUT] touch2Index 0 - 9 if valid, -1 if invalid
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
//----------------------------------------------------------
static const CPU_INT08S touchInput()
{
    CPU_INT08S touch2Index = -1;
    if (TS_State.TouchDetected)
    {
        if (TS_State.X < 80)
        {
            if (TS_State.Y < 80)
            {
                touch2Index = 0;
            }
            else if (TS_State.Y < 160)
            {
                touch2Index = 3;
            }
            else if (TS_State.Y < 240)
            {
                touch2Index = 6;
            }
        }
        else if (TS_State.X < 160)
        {
            if (TS_State.Y < 80)
            {
                touch2Index = 1;
            }
            else if (TS_State.Y < 160)
            {
                touch2Index = 4;
            }
            else if (TS_State.Y < 240)
            {
                touch2Index = 7;
            }
        }
        else if (TS_State.X < 240)
        {
            if (TS_State.Y < 80)
            {
                touch2Index = 2;
            }
            else if (TS_State.Y < 160)
            {
                touch2Index = 5;
            }
            else if (TS_State.Y < 240)
            {
                touch2Index = 8;
            }
        }
    }
    return touch2Index;
}
//----------------------------------------------------------
//! A simple Debug logger for detected touch screen position
//! \param [IN] mask - mask for desired log info
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
//----------------------------------------------------------
static void logger(const uint8_t mask)
{
    BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
    BSP_LCD_SetFont(&Font16);
    uint8_t log[64];
    if (mask & LOG_TOUCH_SCREEN)
    {
        if (TS_State.TouchDetected)
        {
            sprintf((char *)log, "x:%u, y:%u, z:%u", TS_State.X, TS_State.Y, TS_State.Z);
            BSP_LCD_DisplayStringAt(0, 10, log, LEFT_MODE);
        }
    }
    if (mask & LOG_BOARD_DATA_CORRUPT)
    {
        sprintf((char *)log, "Error! GameBoard data corruption!");
        BSP_LCD_DisplayStringAt(0, 15, log, LEFT_MODE);
    }
    // TO-DO Extra logs
    //  if (mask & LOG_CPU_STATUS)
    //  {

    // }
    // if (mask & LOG_TASK_COUNT)
    // {

    // }
    // if (mask & LOG_CONTEXT_SWITCH)
    // {

    // }
}

void HAL_Delay(uint32_t Delay)
{
    OS_ERR err;
    OSTimeDly((OS_TICK)Delay,
              (OS_OPT)OS_OPT_TIME_DLY,
              (OS_ERR *)&err);
}

static void PrintResult(uint8_t who)
{
    BSP_LCD_SetTextColor(LCD_COLOR_RED);
    BSP_LCD_SetFont(&Font24);

    switch (who)
    {
    case 0:
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 280, (uint8_t *)"Tie!", CENTER_MODE);
        break;

    case 1:
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 280, (uint8_t *)"Bot Won!", CENTER_MODE);
        break;

    case 2:
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 280, (uint8_t *)"Human Won!", CENTER_MODE);
        break;
    }

    GameOver();
}

static void GameOver(void)
{
    OS_ERR err;

    OSTaskDel((OS_TCB *)&DrawBoardTCB,
              (OS_ERR *)&err);

    OSTaskDel((OS_TCB *)&BotPlayerTCB,
              (OS_ERR *)&err);

    OSTaskDel((OS_TCB *)&HumanPlayerTCB,
              (OS_ERR *)&err);

    OSTaskDel((OS_TCB *)&AnalysisTCB,
              (OS_ERR *)&err);
}

/*
*********************************************************************************************************
*                                      System Initializations
*********************************************************************************************************
*/

static void LCD_Init(void)
{
    BSP_LCD_Init();
    BSP_LCD_LayerDefaultInit(LCD_BACKGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_LayerDefaultInit(LCD_FOREGROUND_LAYER, LCD_FRAME_BUFFER);
    BSP_LCD_SelectLayer(LCD_FOREGROUND_LAYER);
    BSP_LCD_DisplayOn();
    BSP_LCD_Clear(LCD_COLOR_WHITE);
    BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /** Configure the main internal regulator output voltage
     */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    /** Initializes the CPU, AHB and APB busses clocks
     */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 180;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /** Activate the Over-Drive mode
     */
    HAL_PWREx_EnableOverDrive();

    /** Initializes the CPU, AHB and APB busses clocks
     */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    PeriphClkInitStruct.PLLSAI.PLLSAIN = 216;
    PeriphClkInitStruct.PLLSAI.PLLSAIR = 2;
    PeriphClkInitStruct.PLLSAIDivR = RCC_PLLSAIDIVR_2;
    HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
}
