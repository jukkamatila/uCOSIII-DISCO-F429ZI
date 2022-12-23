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
#define APP_TASK_START_PRIO 10u
#define TOUCH_INPUT_PRIO 11u
#define ANALYSIS_PRIO 12u
#define GAME_RUN_PRIO 13u
#define DRAW_SNAKE_PRIO 14u
#define DRAW_APPLE_PRIO 15u

/* Debug logger */
// typedef unsigned char LOG_MASK    //typedef not working
#define LOG_TOUCH_SCREEN (uint8_t)0x01
#define LOG_BOARD_DATA_CORRUPT (uint8_t)0x02
#define LOG_ERR_ANALYS (uint8_t)0x04
#define LOG_CPU_STATUS (uint8_t)0x20
#define LOG_TASK_COUNT (uint8_t)0x40
#define LOG_CONTEXT_SWITCH (uint8_t)0x80
#define LOG_OS_STATUS (uint8_t)0xe0
#define LOG_ALL (uint8_t)0xff
#define LOG_EN 1u

/* Data structures */
typedef struct tuples
{
    CPU_INT16S X;
    CPU_INT16S Y;
} tuple;

typedef struct snake_body_nodes
{
    tuple coordinates;
    struct snake_body_nodes *next_node;
} snake_body_node;

typedef struct snake_head_nodes
{
    CPU_INT16S speed;
    tuple direction;
    tuple coordinates;
    struct snake_body_nodes *next_node;
} snake_head_node;

typedef struct game_data
{
    snake_head_node *snake_head;
    tuple *apple;
} gamedata_t;

/* Game Parameters */
#define SCALE (CPU_INT16S)10 // Map the actual pixel to a square of the grid
#define HALF_SCALE (CPU_INT16S)5
#define DIR_LEFT \
    (tuple)      \
    {            \
        0, 1     \
    }
#define DIR_RIGHT \
    (tuple)       \
    {             \
        0, -1     \
    }
#define DIR_DOWN \
    (tuple)      \
    {            \
        1, 0     \
    }
#define DIR_UP \
    (tuple)    \
    {          \
        -1, 0  \
    }
#define ZERO (CPU_INT16S)0
#define SNAKE_START_SPEED (CPU_INT16S)1
#define SNAKE_START_DIRECTION DIR_LEFT
#define SNAKE_X_OFFSET 0 - HALF_SCALE // OFFSET for centering the each squre of the grid
#define SNAKE_Y_OFFSET HALF_SCALE
#define SNAKE_START_COORDINATES \
    (tuple)                     \
    {                           \
        115, 115                \
    }

#define APPLE_START_COORDINATES \
    (tuple)                     \
    {                           \
        5, 5                    \
    }

/*
*********************************************************************************************************
*                                           GLOBAL VARIABLES
*********************************************************************************************************
*/

/* Task Control Block */
static OS_TCB AppTaskStartTCB;
static OS_TCB TouchInputTCB;
static OS_TCB GameRunTCB;
static OS_TCB DrawSnakeTCB;
static OS_TCB DrawAppleTCB;
static OS_TCB AnalysisTCB;

/* Task Stack */
static CPU_STK AppTaskStartStk[TASK_STK_SIZE];
static CPU_STK TouchInputStk[TASK_STK_SIZE];
static CPU_STK GameRunStk[TASK_STK_SIZE];
static CPU_STK DrawSnakeStk[TASK_STK_SIZE];
static CPU_STK DrawAppleStk[TASK_STK_SIZE];
static CPU_STK AnalysisStk[TASK_STK_SIZE];

static TS_StateTypeDef TS_State;

OS_MUTEX mutex_apple;
OS_MUTEX mutex_snake;

/*
*********************************************************************************************************
*                                         FUNCTION PROTOTYPES
*********************************************************************************************************
*/

/* Task Prototypes */
static void AppTaskStart(void *p_arg);
static void TouchInput(void *p_arg);
static void GameRun(void *p_arg);
static void DrawSnake(void *p_arg);
static void DrawApple(void *p_arg);
static void Analysis(void *p_arg);

/* System Initilization Prototypes */
static void SystemClock_Config(void);
static void LCD_Init(void);

/* Game Functions */
static void Snake_Init(snake_head_node *const snake_head);
static void AddSnakeBody(snake_head_node *const snake_head);
static void DestroySnake(snake_head_node *const snake_head);
static void AppleCoordinatesUpdate(tuple *const apple);
static void SnakeCoordinatesUpdate(snake_head_node *const snake_head);
static const CPU_BOOLEAN TryEatApple(const snake_head_node *const snake_head, const tuple *const apple);
static void GameOver(void);
static void logger(const uint8_t mask);
static const CPU_BOOLEAN TupleComp(const tuple *const tuple1, const tuple *const tuple2);

/*
*********************************************************************************************************
*                                                MAIN
*********************************************************************************************************
*/

int main(void)
{
    OS_ERR err;

    OSInit(&err);

    // mutex is actually not needed in this App at all. Because each Task run on their own turn by Task Control Block, there is no cocurrency. But I decided to leave the original code for case study
    OSMutexCreate((OS_MUTEX *)&mutex_apple,
                  (CPU_CHAR *)"Mutex for apple",
                  (OS_ERR *)&err);

    OSMutexCreate((OS_MUTEX *)&mutex_snake,
                  (CPU_CHAR *)"Mutex for snake",
                  (OS_ERR *)&err);

    OSTaskCreate((OS_TCB *)&AppTaskStartTCB,
                 (CPU_CHAR *)"App Task Start",
                 (OS_TASK_PTR)AppTaskStart,
                 (void *)NULL,
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
    snake_head_node snake_head = {SNAKE_START_SPEED, SNAKE_START_DIRECTION, SNAKE_START_COORDINATES, NULL};
    tuple apple = APPLE_START_COORDINATES;
    gamedata_t data = {&snake_head, &apple};

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
        OSTaskCreate((OS_TCB *)&TouchInputTCB,
                     (CPU_CHAR *)"TouchInput Task",
                     (OS_TASK_PTR)TouchInput,
                     (void *)&data,
                     (OS_PRIO)TOUCH_INPUT_PRIO,
                     (CPU_STK *)&TouchInputStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&GameRunTCB,
                     (CPU_CHAR *)"GameRun Task",
                     (OS_TASK_PTR)GameRun,
                     (void *)&data,
                     (OS_PRIO)GAME_RUN_PRIO,
                     (CPU_STK *)&GameRunStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&DrawSnakeTCB,
                     (CPU_CHAR *)"DrawSnake Task",
                     (OS_TASK_PTR)DrawSnake,
                     (void *)&data,
                     (OS_PRIO)DRAW_SNAKE_PRIO,
                     (CPU_STK *)&DrawSnakeStk[0],
                     (CPU_STK_SIZE)TASK_STK_SIZE / 10,
                     (CPU_STK_SIZE)TASK_STK_SIZE,
                     (OS_MSG_QTY)5u,
                     (OS_TICK)0u,
                     (void *)0,
                     (OS_OPT)(OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR),
                     (OS_ERR *)&err);

        OSTaskCreate((OS_TCB *)&DrawAppleTCB,
                     (CPU_CHAR *)"DrawApple Task",
                     (OS_TASK_PTR)DrawApple,
                     (void *)&data,
                     (OS_PRIO)DRAW_APPLE_PRIO,
                     (CPU_STK *)&DrawAppleStk[0],
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
                     (void *)&data,
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

/**
 * @brief Converts user touch input to UP, DOWN, LEFT, RIGHT
 * @param [IN] p_arg - cast to gamedata_t* before use
 * @author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void TouchInput(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    snake_head_node *snake_head = ((gamedata_t *)p_arg)->snake_head;
    while (DEF_TRUE)
    {
        BSP_TS_GetState(&TS_State);
        // logger(LOG_TOUCH_SCREEN);

        if (TS_State.TouchDetected)
        {
            OSMutexPend((OS_MUTEX *)&mutex_snake,
                        (OS_TICK)0,
                        (OS_OPT)OS_OPT_PEND_BLOCKING,
                        (CPU_TS *)&ts,
                        (OS_ERR *)&err);

            if (TS_State.Y < 75)
            {
                if (TupleComp(&(snake_head->direction), &DIR_UP) || TupleComp(&(snake_head->direction), &DIR_DOWN))     // can turn LEFT only if current direction is UP or DOWN
                    snake_head->direction = DIR_LEFT;
            }
            else if (TS_State.Y < 225)
            {
                if ((TupleComp(&(snake_head->direction), &DIR_LEFT) || TupleComp(&(snake_head->direction), &DIR_RIGHT)))    // can turn UP/DOWN only if current direction is LEFT or RIGHT
                {
                    if (TS_State.X < 120)
                    {

                        snake_head->direction = DIR_UP;
                    }
                    else
                    {
                        snake_head->direction = DIR_DOWN;
                    }
                }
            }
            else if (TS_State.Y >= 225)
            {
                if ((TupleComp(&(snake_head->direction), &DIR_UP) || TupleComp(&(snake_head->direction), &DIR_DOWN)))       // can turn RIGHT only if current direction is UP or DOWN
                    snake_head->direction = DIR_RIGHT;
            }

            OSMutexPost((OS_MUTEX *)&mutex_snake,
                        (OS_OPT)OS_OPT_POST_NONE,
                        (OS_ERR *)&err);
        }
        OSTimeDly(1, OS_OPT_TIME_DLY, &err);
    }
}

/**
 * \brief Update the status of the snake and the apple
 * \param [IN] p_arg - cast to gamedata_t* before use
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void GameRun(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    snake_head_node *snake_head = ((gamedata_t *)p_arg)->snake_head;
    // snake_body_node *snake_body;
    tuple *apple = ((gamedata_t *)p_arg)->apple;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        SnakeCoordinatesUpdate(snake_head);

        OSMutexPend((OS_MUTEX *)&mutex_apple,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        // if (TryEatApple(snake_head, (tuple *)apple))
        // {
        //     AppleCoordinatesUpdate((tuple *)apple);
        // }

        OSMutexPost((OS_MUTEX *)&mutex_apple,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSMutexPost((OS_MUTEX *)&mutex_snake,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSTimeDlyHMSM(
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)1,
            (CPU_INT16U)0,
            OS_OPT_TIME_DLY,
            &err);
    }
}

/*---------------------------------------------------*/
//! \brief Draw snake to the LCD Screen
//! \param [IN] p_arg - cast to gamedata_t* before use
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*---------------------------------------------------*/
static void DrawSnake(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    snake_head_node *snake_head = ((gamedata_t *)p_arg)->snake_head;
    snake_body_node *snake_body;

    while (DEF_TRUE)
    {
        BSP_LCD_Clear(LCD_COLOR_BLACK);
        BSP_LCD_SetTextColor(LCD_COLOR_BLUE);
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        snake_body = snake_head->next_node;
        BSP_LCD_FillRect(snake_head->coordinates.X + SNAKE_X_OFFSET, snake_head->coordinates.Y + SNAKE_Y_OFFSET, SCALE, SCALE);
        while (snake_body != NULL)
        {
            BSP_LCD_DrawRect(snake_body->coordinates.X + SNAKE_X_OFFSET, snake_body->coordinates.Y + SNAKE_Y_OFFSET, SCALE, SCALE);
            snake_body = snake_body->next_node;
        }

        OSMutexPost((OS_MUTEX *)&mutex_snake,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSTimeDlyHMSM(
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)20,
            OS_OPT_TIME_DLY,
            &err);
    }
}

/*---------------------------------------------------*/
//! \brief Draw apple to the LCD Screen
//! \param [IN] p_arg - cast to gamedata_t* before use
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*---------------------------------------------------*/
static void DrawApple(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    tuple *apple = ((gamedata_t *)p_arg)->apple;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_apple,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        BSP_LCD_SetTextColor(LCD_COLOR_RED);
        BSP_LCD_FillCircle(apple->X, apple->Y, HALF_SCALE);

        OSTimeDlyHMSM(
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)200,
            OS_OPT_TIME_DLY,
            &err);

        BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
        BSP_LCD_FillCircle(apple->X, apple->Y, HALF_SCALE);

        OSMutexPost((OS_MUTEX *)&mutex_apple,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSTimeDlyHMSM(
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)200,
            OS_OPT_TIME_DLY,
            &err);
    }
}

/*---------------------------------------------------*/
//! \brief Check if snake's head hit it's body
//! \param [IN] p_arg - cast to gamedata_t* before use
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*---------------------------------------------------*/
static void Analysis(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        // TBD - check if snake head hits the body

        OSMutexPost((OS_MUTEX *)&mutex_snake,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

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
/*-------------------------------------------------------------------*/
//! Update the coordinates of the snake, nonreentrant/not thread safe
//! \param [IN] snake_head_node
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*------------------------------------------------------------------*/
static void SnakeCoordinatesUpdate(snake_head_node *const snake_head)
{
    tuple temp_coordinates = snake_head->coordinates;
    snake_body_node *snake_body = snake_head->next_node;

    snake_head->coordinates.X += snake_head->speed * SCALE * snake_head->direction.X;
    if (snake_head->coordinates.X > 240)
        snake_head->coordinates.X = 5;
    if (snake_head->coordinates.X < 0)
        snake_head->coordinates.X = 235;

    snake_head->coordinates.Y += snake_head->speed * SCALE * snake_head->direction.Y;
    if (snake_head->coordinates.Y > 320)
        snake_head->coordinates.Y = 5;
    if (snake_head->coordinates.Y < 0)
        snake_head->coordinates.Y = 315;

    // TBD - Snake body coordinates update
    // addBody(snake_head)
    // malloc new_body_node;
    // new_body_node->coordinates = temp_coordinates;
    // new_body_node->next = snake_head->next;
    // snake_heas->next = new_body_node;

    // if (snake_body)
    // {

    //     snake_body->coordinates = snake_head->coordinates;
    //     snake_body = snake_body->next_node;
    //     while (snake_body)
    //     {
    //         snake_body->coordinates = temp_coordinates;

    //         snake_body = snake_body->next_node;
    //     }
    // }
}

/*---------------------------------------------------------------*/
//! Check if snake eats the apple, nonreentrant/not thread safe
//! \param [IN] snake_head_node
//! \param [IN] apple
//! \return TRUE or FALSE
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*---------------------------------------------------------------*/
static const CPU_BOOLEAN TryEatApple(const snake_head_node *const snake_head, const tuple *const apple)
{
    return OS_TRUE;
}

/**
 * \brief Compare two tuples
 * \param [IN] tuple1
 * \param [IN] tuple2
 * \return Return 1 if the same, 0 if not
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static const CPU_BOOLEAN TupleComp(const tuple *const tuple1, const tuple *const tuple2)
{
    if (tuple1->X == tuple2->X && tuple1->Y == tuple2->Y)
    {
        return (CPU_BOOLEAN)1u;
    }
    else
    {
        return (CPU_BOOLEAN)0u;
    }
}

/*----------------------------------------------------------*/
//! A simple Debug logger for detected touch screen position
//! \param [IN] mask - mask for desired log info
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*----------------------------------------------------------*/
static void logger(const uint8_t mask)
{
    BSP_LCD_SetTextColor(LCD_COLOR_GREEN);
    BSP_LCD_SetFont(&Font12);
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

    if (mask & LOG_ERR_ANALYS)
    {
        sprintf((char *)log, "Error! Analysis turn data corruption!");
        BSP_LCD_DisplayStringAt(0, 20, log, LEFT_MODE);
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

/*--------------------------------------------------*/
//! Print game result
//! \param [IN] result
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*--------------------------------------------------*/
static void PrintResult(uint8_t result)
{
    BSP_LCD_SetTextColor(LCD_COLOR_RED);
    BSP_LCD_SetFont(&Font12);

    switch (result)
    {
    case 0:
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 280, (uint8_t *)"WON!", CENTER_MODE);
        break;
    case 1:
        BSP_LCD_DisplayStringAt(0, BSP_LCD_GetYSize() - 280, (uint8_t *)"LOOSE!", CENTER_MODE);
        break;
    default:
        // the code should never come herer
        break;
    }

    GameOver();
}

/*--------------------------------------------------*/
//! Delete the tasks and end the game
//! \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
/*--------------------------------------------------*/
static void GameOver(void)
{
    OS_ERR err;

    OSTaskDel((OS_TCB *)&DrawSnakeTCB,
              (OS_ERR *)&err);

    OSTaskDel((OS_TCB *)&DrawAppleTCB,
              (OS_ERR *)&err);

    OSTaskDel((OS_TCB *)&GameRunTCB,
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
    BSP_LCD_Clear(LCD_COLOR_BLACK);
    BSP_LCD_SetTextColor(LCD_COLOR_WHITE);
    BSP_LCD_SetFont(&Font12);
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
