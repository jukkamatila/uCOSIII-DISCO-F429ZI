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
#define LOG_ERR_MEMORY (uint8_t)0x08
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
} tuple_t;

typedef struct apples
{
    tuple_t coordinates;
    uint32_t colour; // snake get a new body with the same colour as the eaten apple
} apple_t;

typedef struct snake_body_nodes
{
    tuple_t coordinates;
    uint32_t colour;
    struct snake_body_nodes *previous_node;
    struct snake_body_nodes *next_node;
} snake_body_node_t;

typedef struct snakes
{
    CPU_INT16U length;
    CPU_INT16S speed;
    tuple_t direction;
    snake_body_node_t *head;
    snake_body_node_t *tail;
} snake_t;

typedef struct game_data
{
    snake_t *snake;
    apple_t *apple;
} gamedata_t;

/* Game Parameters */
#define SCALE (CPU_INT16S)10 // Map the actual pixel to a square of the grid
#define HALF_SCALE (CPU_INT16S)5
#define LENGTH_MAX 32 * 24
#define INTEVAL_MILLISEC_MAX 999
#define INTEVAL_MILLISEC_MIN 100
#define INTEVAL_MILLISEC_START 500
#define DIR_LEFT \
    (tuple_t)    \
    {            \
        0, 1     \
    }
#define DIR_RIGHT \
    (tuple_t)     \
    {             \
        0, -1     \
    }
#define DIR_DOWN \
    (tuple_t)    \
    {            \
        1, 0     \
    }
#define DIR_UP \
    (tuple_t)  \
    {          \
        -1, 0  \
    }
typedef CPU_INT08U gameresult_t;
#define COLLISION_NO (CPU_INT08U)0
#define COLLISION_SNAKE_HEAD (CPU_INT08U)1
#define COLLISION_SNAKE_BODY (CPU_INT08U)2
#define RESULT_WON (gameresult_t)0
#define RESULT_LOST (gameresult_t)1
#define APPLE_START_COLOUR LCD_COLOR_RED
#define SNAKE_START_SPEED (CPU_INT16S)1
#define SNAKE_START_LENGTH (CPU_INT16U)1
#define SNAKE_HEAD_COLOUR LCD_COLOR_GREEN
#define SNAKE_START_DIRECTION DIR_LEFT
#define SNAKE_X_OFFSET 0 - HALF_SCALE // OFFSET for centering the each squre of the grid
#define SNAKE_Y_OFFSET 0 - HALF_SCALE
#define SNAKE_START_COORDINATES \
    (tuple_t)                   \
    {                           \
        115, 115                \
    }

#define APPLE_START_COORDINATES \
    (tuple_t)                   \
    {                           \
        115, 205                \
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
static void SnakeInit(snake_t *const snake);
static void AppleInit(apple_t *const apple);
static void SnakeCoordinatesUpdate(snake_t *const snake);
static void SnakeSpeedUpdate(snake_t *const snake);
static snake_body_node_t *NewSnakeNode(void);
static void AddSnakeBodyNode(snake_t *const snake);
static const CPU_INT08U SnakeCollisionCheck(const snake_body_node_t *const snake_head, const tuple_t *const point);
static void FreeSnake(snake_body_node_t *snake_head);
static void AppleCoordinatesUpdate(gamedata_t *const game_data);
static void AppleColourUpdate(apple_t *const apple);
static const CPU_BOOLEAN TryEatApple(snake_t *const snake, const apple_t *const apple);
static void GameOver(void *p_arg);
static void PrintResult(const gameresult_t result);
static void logger(const uint8_t mask);
static const CPU_BOOLEAN TupleCompare(const tuple_t *const tuple1, const tuple_t *const tuple2);
static CPU_INT16U NodeDistance(const tuple_t *const point1, const tuple_t *const point2);

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
    snake_t snake;
    apple_t apple;
    gamedata_t data = {&snake, &apple};

    AppleInit(&apple);
    SnakeInit(&snake);

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
                     (void *)data.snake,
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
                     (void *)data.snake,
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
                     (void *)data.apple,
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
                     (void *)data.snake,
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

    while (DEF_TRUE)
    {
        OSTimeDly(1, OS_OPT_TIME_DLY, &err);
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
    snake_t *snake = ((snake_t *)p_arg);
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
                if (TupleCompare(&(snake->direction), &DIR_UP) || TupleCompare(&(snake->direction), &DIR_DOWN)) // can turn LEFT only if current direction is UP or DOWN
                    snake->direction = DIR_LEFT;
            }
            else if (TS_State.Y < 225)
            {
                if ((TupleCompare(&(snake->direction), &DIR_LEFT) || TupleCompare(&(snake->direction), &DIR_RIGHT))) // can turn UP/DOWN only if current direction is LEFT or RIGHT
                {
                    if (TS_State.X < 120)
                    {

                        snake->direction = DIR_UP;
                    }
                    else
                    {
                        snake->direction = DIR_DOWN;
                    }
                }
            }
            else if (TS_State.Y >= 225)
            {
                if ((TupleCompare(&(snake->direction), &DIR_UP) || TupleCompare(&(snake->direction), &DIR_DOWN))) // can turn RIGHT only if current direction is UP or DOWN
                    snake->direction = DIR_RIGHT;
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
    CPU_INT16U interval = INTEVAL_MILLISEC_START;
    snake_t *snake = ((gamedata_t *)p_arg)->snake;
    apple_t *apple = ((gamedata_t *)p_arg)->apple;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);
        
        if(interval > INTEVAL_MILLISEC_MIN)
        {
            interval = INTEVAL_MILLISEC_START - snake->speed;
        }

        SnakeCoordinatesUpdate(snake);
        SnakeSpeedUpdate(snake);

        OSMutexPend((OS_MUTEX *)&mutex_apple,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        if (TryEatApple(snake, apple))
        {
            AppleCoordinatesUpdate((gamedata_t *)p_arg);
            AppleColourUpdate(apple);
        }

        OSMutexPost((OS_MUTEX *)&mutex_apple,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSMutexPost((OS_MUTEX *)&mutex_snake,
                    (OS_OPT)OS_OPT_POST_NONE,
                    (OS_ERR *)&err);

        OSTimeDlyHMSM(
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)0,
            (CPU_INT16U)interval,
            OS_OPT_TIME_DLY,
            &err);
    }
}

/**
 * \brief Draw snake to the LCD Screen
 * \param [IN] p_arg - cast to gamedata_t* before use
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void DrawSnake(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    snake_t *snake = (snake_t *)p_arg;
    snake_body_node_t *snake_node;

    while (DEF_TRUE)
    {
        BSP_LCD_Clear(LCD_COLOR_BLACK);
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);
        snake_node = snake->head;
        while (snake_node != NULL)
        {
            BSP_LCD_SetTextColor(snake_node->colour);
            BSP_LCD_FillRect(snake_node->coordinates.X + SNAKE_X_OFFSET, snake_node->coordinates.Y + SNAKE_Y_OFFSET, SCALE, SCALE);
            snake_node = snake_node->next_node;
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

/**
 * \brief Testing Circle and Rect offset
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void DrawTest(void)
{
    BSP_LCD_FillCircle(100, 100, HALF_SCALE);
    BSP_LCD_FillRect(100 + SNAKE_X_OFFSET , 100 + SNAKE_Y_OFFSET, SCALE, SCALE);
}

/**
 * \brief Draw apple to the LCD Screen
 * \param [IN] p_arg - cast to gamedata_t* before use
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void DrawApple(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;
    apple_t *apple = (apple_t *)p_arg;

    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_apple,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        BSP_LCD_SetTextColor(apple->colour);
        BSP_LCD_FillCircle(apple->coordinates.X, apple->coordinates.Y, HALF_SCALE);
        // DrawTest();

        OSMutexPost((OS_MUTEX *)&mutex_apple,
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

/**
 * \brief Check if snake's head hit it's body
 * \param [IN] p_arg - cast to gamedata_t* before use
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void Analysis(void *p_arg)
{
    OS_ERR err;
    CPU_TS ts;

    snake_t *snake = (snake_t *)p_arg;
    while (DEF_TRUE)
    {
        OSMutexPend((OS_MUTEX *)&mutex_snake,
                    (OS_TICK)0,
                    (OS_OPT)OS_OPT_PEND_BLOCKING,
                    (CPU_TS *)&ts,
                    (OS_ERR *)&err);

        // TBD - check if snake head hits the body
        if (SnakeCollisionCheck(snake->head, &(snake->head->coordinates)) == COLLISION_SNAKE_BODY)
        {
            // game over
            PrintResult(RESULT_LOST);
            GameOver(p_arg);
        }

        if (snake->length == LENGTH_MAX)
        {
            PrintResult(RESULT_WON);
            GameOver(p_arg);
        }

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
/**
 * \brief a snake, nonreentrant/not thread safe
 * \param [IN] *snake
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void SnakeInit(snake_t *const snake)
{
    snake->length = SNAKE_START_LENGTH;
    snake->speed = SNAKE_START_SPEED;
    snake->direction = SNAKE_START_DIRECTION;
    snake->head = NewSnakeNode();
    snake->head->coordinates = SNAKE_START_COORDINATES;
    snake->head->colour = SNAKE_HEAD_COLOUR;
    snake->head->previous_node = NULL;
    snake->head->next_node = NULL;
    snake->tail = snake->head;
}

/**
 * \brief an apple, nonreentrant/not thread safe
 * \param [IN] *apple
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void AppleInit(apple_t *apple)
{
    apple->coordinates = APPLE_START_COORDINATES;
    apple->colour = APPLE_START_COLOUR;
}

/**
 * \brief Check for collision of a point and the snake, nonreentrant/not thread safe
 * \param [IN] *snake_head
 * \param [IN] point - a point on the map
 * \return collision_type
 * \details
 * -COLLISION_NO
 * -COLLISION_SNAKE_HEAD
 * -COLLISION_SNAKE_BODY
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static const CPU_INT08U SnakeCollisionCheck(const snake_body_node_t *const snake_head, const tuple_t *const point)
{
    CPU_INT08U collision_type = COLLISION_NO;
    snake_body_node_t *snake_node;

    // check the head
    if (TupleCompare(&(snake_head->coordinates), point))
    {
        collision_type = COLLISION_SNAKE_HEAD;
    }

    // check the body
    snake_node = snake_head;
    while (snake_node != NULL)
    {
        snake_node = snake_node->next_node;
        if (TupleCompare(&(snake_node->coordinates), point))
        {
            collision_type = COLLISION_SNAKE_BODY;
            break;
        }
    }

    return collision_type;
}

/**
 * \brief Update apple's coordinates, nonreentrant/not thread safe
 * \param [IN] *apple
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void AppleCoordinatesUpdate(gamedata_t *const game_data)
{
    tuple_t new_coordinates;
    new_coordinates.X = 5 + (rand() % 24) * SCALE;
    new_coordinates.Y = 5 + (rand() % 32) * SCALE;
    while (SnakeCollisionCheck(game_data->snake->head, &new_coordinates))
    {
        new_coordinates.X = 5 + (rand() % 24) * SCALE;
        new_coordinates.Y = 5 + (rand() % 32) * SCALE;
    }
    game_data->apple->coordinates = new_coordinates;
}

static void AppleColourUpdate(apple_t *const apple)
{
    uint32_t colours[7] = {
        LCD_COLOR_RED,
        LCD_COLOR_ORANGE,
        LCD_COLOR_YELLOW,
        LCD_COLOR_GREEN,
        LCD_COLOR_BLUE,
        LCD_COLOR_MAGENTA,
        LCD_COLOR_WHITE};
    int32_t option = rand() % 7;
    apple->colour = colours[option];
}

/**
 * \brief update the coordinates of the snake, nonreentrant/not thread safe
 * \param [IN] snake
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void SnakeCoordinatesUpdate(snake_t *const snake)
{
    snake_body_node_t *snake_node = snake->tail;

    // Update the coordinates from the tail to head->next
    while (snake_node != snake->head)
    {
        snake_node->coordinates = snake_node->previous_node->coordinates;
        snake_node = snake_node->previous_node;
    }

    // Update the head
    snake->head->coordinates.X += SCALE * snake->direction.X;
    if (snake->head->coordinates.X > 240)
    {
        snake->head->coordinates.X = 5;
    }
    if (snake->head->coordinates.X < 0)
    {
        snake->head->coordinates.X = 235;
    }

    snake->head->coordinates.Y += SCALE * snake->direction.Y;
    if (snake->head->coordinates.Y > 320)
    {
        snake->head->coordinates.Y = 5;
    }
    if (snake->head->coordinates.Y < 0)
    {
        snake->head->coordinates.Y = 315;
    }
}

/**
 * \brief update the speed of the snake, nonreentrant/not thread safe
 * \param [IN] snake
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void SnakeSpeedUpdate(snake_t *const snake)
{
    snake->speed = snake->length + snake->length;
}

/**
 * \brief very fast approximation of squreroot precision to unsigned integer
 * \details Newton-Raphson method
 * \param [IN] number - an unsigned 16bit number
 * \return result - the sqrt of the number
 * \author wildplasser@StackOverflow, ported by Siyuan XU, e2101066@edu.vamk.fi, 12.2022
 * \ref https://stackoverflow.com/questions/34187171/fast-integer-square-root-approximation
 **/
static CPU_INT16U FastSQRT(CPU_INT16U number)
{
    CPU_INT16U result, temp;

    if (number < 2)
        return number; /* avoid div/0 */

    result = 100; /* starting point is relatively unimportant */

    temp = number / result;
    result = (result + temp) >> 1;
    temp = number / result;
    result = (result + temp) >> 1;
    temp = number / result;
    result = (result + temp) >> 1;
    temp = number / result;
    result = (result + temp) >> 1;

    return result;
}

/**
 * \brief the distance between two nodes
 * \param [IN] point1 - a point with X, Y cooridnates
 * \param [IN] point2 - a point with X, Y cooridnates
 * \return the distance between two nodes
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 **/
static CPU_INT16U NodeDistance(const tuple_t *const point1, const tuple_t *const point2)
{
    CPU_INT16U distance = FastSQRT((point2->Y - point1->Y) * (point2->Y - point1->Y) + (point2->X - point1->X) * (point2->X - point1->X));
    return distance;
}

/**
 * \brief Produce a new snake node on the heap
 * \return *new_snake_node
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 **/
static snake_body_node_t *NewSnakeNode(void)
{
    snake_body_node_t *new_snake_node = (snake_body_node_t *)malloc(sizeof(snake_body_node_t));
    if (new_snake_node == NULL)
    {
        // Error handling
        logger(LOG_ERR_MEMORY);
    }

    return new_snake_node;
}

/**
 * \brief add a snake body node to the end
 * \param [IN] *snake
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 **/
static void AddSnakeBodyNode(snake_t *const snake)
{
    snake_body_node_t *new_body_node = NewSnakeNode();
    if (snake->length > 1)
    {
        new_body_node->coordinates.X = snake->tail->coordinates.X + (snake->tail->coordinates.X - snake->tail->previous_node->coordinates.X);
        new_body_node->coordinates.Y = snake->tail->coordinates.Y + (snake->tail->coordinates.Y - snake->tail->previous_node->coordinates.Y);
    }
    else
    {
        new_body_node->coordinates.X = snake->tail->coordinates.X - SCALE * snake->direction.X;
        new_body_node->coordinates.Y = snake->tail->coordinates.Y - SCALE * snake->direction.Y;
    }

    new_body_node->previous_node = snake->tail;
    new_body_node->next_node = NULL;
    snake->tail->next_node = new_body_node;
    snake->tail = new_body_node;
    snake->length++;
}

/**
 * \brief Free the heap allocated to snake
 * \param [IN] *snake_head
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 **/
static void FreeSnake(snake_body_node_t *snake_head)
{
    snake_body_node_t *snake_node_to_free;

    while (snake_head != NULL)
    {
        snake_node_to_free = snake_head;
        snake_head = snake_head->next_node;
        free(snake_node_to_free);
    }
}

/**
 * Check if snake eats the apple, nonreentrant/not thread safe
 * \param [IN] snake
 * \param [IN] apple
 * \return TRUE or FALSE
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static const CPU_BOOLEAN TryEatApple(snake_t *const snake, const apple_t *const apple)
{
    // if (NodeDistance(&snake->head->coordinates, &apple->coordinates) == 0)
    if (TupleCompare(&snake->head->coordinates, &apple->coordinates))
    {
        AddSnakeBodyNode(snake);
        snake->tail->colour = apple->colour;
        return OS_TRUE;
    }
    return OS_FALSE;
}

/**
 * \brief Compare two tuples
 * \param [IN] tuple1
 * \param [IN] tuple2
 * \return Return 1 if the same, 0 if not
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static const CPU_BOOLEAN TupleCompare(const tuple_t *const tuple1, const tuple_t *const tuple2)
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

/**
 * A simple Debug logger for detected touch screen position
 * \param [IN] mask - mask for desired log info
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
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

    if (mask & LOG_ERR_MEMORY)
    {
        sprintf((char *)log, "Error! Memory allocation failed!");
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

/**
 * Print game result
 * \param [IN] result
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void PrintResult(const gameresult_t result)
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
}

/**
 * Delete the tasks and end the game
 * \author siyuan xu, e2101066@edu.vamk.fi, 12.2022
 */
static void GameOver(void *p_arg)
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

    FreeSnake(((gamedata_t *)p_arg)->snake->head);
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
