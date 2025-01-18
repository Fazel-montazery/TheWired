#include <ncurses.h>
#include <form.h>
#include <stdarg.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <ctype.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#include "ansi_colors.h"

#define MAX_MESSAGE_HISTORY 100
#define MAX_BUFFER_SIZE 1024
#define MAX_NAME_LEN 30

typedef struct {
        // UI
        WINDOW *lainWin;
        WINDOW *sideWin;
        WINDOW *mainWin;
        WINDOW *textWin;
        WINDOW *messageWin;
        FIELD  *textField[2];
        FORM   *textForm;

        bool insertMode;

        // NET
        pthread_t net_thread;
        bool isNetThreadAlive;
        int socket;
        char* send_buffer;
        char* recv_buffer;
} State;

static State state = { 0 };

static void init();
static void loop();
static void finish(int sig);
static void resize(int sig);
static void printLain(WINDOW* win);
static void drawUI();
static void deleteUi();
static void createTextForm(WINDOW *win);
static void drawHelp();
static bool isValidNumber(const char *str);
static unsigned short convertPort(const char *port_str);
static void initConnection(const char* ip, unsigned short port, const char* name);
static bool sendMsg(const char* format, ...);
static void* handleConnection(void* vargp);

static const short lain_art_w = 30;
static const short lain_art_h = 18;
static const char* lain_art[] = {
"⣿⣿⡑⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠘⣿",
"⣿⡧⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣟",
"⣿⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸",
"⣿⡇⠀⠀⠀⠀⠀⢀⠂⣸⡇⠀⠀⠀⠀⠀⠀⡀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣹",
"⣿⣷⣧⡀⠀⢀⡀⠸⠆⣸⣗⡱⠀⡀⠀⠀⢀⣧⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⣼",
"⣿⣿⣿⡇⠀⢸⠈⠛⠖⢼⣧⣷⡇⣶⢛⠀⠺⢯⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠠⣹",
"⣿⣿⣿⣷⣼⡀⣧⣤⣤⣼⣿⣿⣿⣿⣿⡀⠛⠶⢬⣄⠀⠀⠀⠀⠀⢌⠀⠀⢀⣿",
"⣿⣿⣿⣿⣿⣷⠹⣿⣯⣿⡿⣿⣿⣿⣿⣷⣤⣀⣤⣾⣾⠀⠀⠀⠰⠂⠀⢠⣸⣿",
"⣿⣿⣿⣿⣿⣿⣷⡙⢿⡿⣐⠛⣿⣿⣿⣿⣿⣿⣿⣿⡗⠀⠀⢀⣀⣀⣄⣾⣿⣿",
"⣿⣿⣿⣿⣿⣿⣿⣿⡆⠻⢿⣿⣻⣿⢿⣿⣿⣿⡿⠟⡈⠀⠀⠀⢹⣿⣿⣿⣿⣿",
"⣿⣿⣿⣿⣿⡿⠟⠉⠀⠀⣮⡛⠷⠻⠛⠛⡉⠠⣐⣼⡆⠀⠀⠀⠈⠻⣿⣿⣿⣿",
"⣿⡿⠟⠋⠁⠀⠀⠀⠀⢰⡿⠙⠀⢡⣬⣴⣶⣿⣿⣿⡇⠀⠀⠀⠀⠀⠈⠻⣿⣿",
"⠉⠀⠀⠀⠀⠀⠀⠀⠀⠈⠀⠀⠀⠈⢿⣽⣻⣿⣿⡿⠁⠀⠀⠀⠀⠀⠀⠐⠘⢿",
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠈⡿⣿⠿⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠉",
"⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠻⠉⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀",
"   ╔╦╗┬ ┬┌─┐  ╦ ╦┬┬─┐┌─┐┌┬┐   ",
"    ║ ├─┤├┤   ║║║│├┬┘├┤  ││   ",
"    ╩ ┴ ┴└─┘  ╚╩╝┴┴└─└─┘─┴┘   "
};

int main(int argc, char *argv[])
{
        if (argc != 4) {
                printf(YEL "Usage: %s <IP> <PORT> <NAME>\n" CRESET, argv[0]);
                exit(EXIT_FAILURE);
        }

        if (strlen(argv[3]) > MAX_NAME_LEN) {
                fprintf(stderr, RED "Error: Name must be at most %d characters long.\n" CRESET, MAX_NAME_LEN);
                exit(EXIT_FAILURE);
        }

        initConnection(argv[1], convertPort(argv[2]), argv[3]);
        pthread_create(&state.net_thread, NULL, handleConnection, NULL);
        init();
        loop();
        finish(0);
}

static void init()
{
        ESCDELAY = 0; // No ESC delay

        signal(SIGINT, finish); // Ctrl+c 
        signal(SIGWINCH, resize); // Terminal resize

        setlocale(LC_ALL,""); // UTF-8

        initscr();
        keypad(stdscr, TRUE);
        nonl();
        cbreak();
        noecho();
        curs_set(FALSE);
        timeout(1);

        if (has_colors())
        {
                start_color();

                init_pair(1, COLOR_RED,     COLOR_BLACK);
                init_pair(2, COLOR_GREEN,   COLOR_BLACK);
                init_pair(3, COLOR_YELLOW,  COLOR_BLACK);
                init_pair(4, COLOR_BLUE,    COLOR_BLACK);
                init_pair(5, COLOR_CYAN,    COLOR_BLACK);
                init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
                init_pair(7, COLOR_WHITE,   COLOR_BLACK);
        }

        drawUI();
}

static void loop()
{
        while (1) {
                int ch = getch();
                if (state.insertMode) {
                        if (ch == 127 || ch == KEY_BACKSPACE) {
                                form_driver(state.textForm, REQ_DEL_PREV);
                        } else if (ch == 27) { // ESC
                                state.insertMode = false;
                                drawHelp();
                                curs_set(FALSE);
                        } else {
                                form_driver(state.textForm, ch);
                        }
                } else {
                        if (ch == 'i' || ch == 'I') {
                                state.insertMode = true;
                                drawHelp();
                                form_driver(state.textForm, REQ_NEXT_CHAR);
                                form_driver(state.textForm, REQ_PREV_CHAR);
                                curs_set(TRUE);
                        } else if (ch == 'q' || ch == 'Q') {
                                return; // End the program
                        }
                }
        }
}

static void finish(int sig)
{
        if (state.send_buffer) free(state.send_buffer);
        if (state.recv_buffer) free(state.recv_buffer);
        close(state.socket);
        deleteUi();
        endwin();
        exit(EXIT_SUCCESS);
}

static void resize(int sig)
{
        curs_set(FALSE);
        state.insertMode = false;

        endwin();            // End ncurses mode
        refresh();           // Refresh the screen
        clear();             // Clear the screen

        deleteUi();
        drawUI();
}

static void printLain(WINDOW* win)
{
        box(win, 0, 0);
        for (int i = 1; i <= lain_art_h; i++) {
                mvwprintw(win, i, 1, lain_art[i - 1]);
        }
}

static void drawUI()
{
        int max_y_stdscr, max_x_stdscr;
        getmaxyx(stdscr, max_y_stdscr, max_x_stdscr);

        // Windows
        WINDOW* lainWin = newwin(lain_art_h + 2, lain_art_w + 2, 1, 1);
        WINDOW* sideWin = newwin(max_y_stdscr - lain_art_h - 5, lain_art_w + 2, lain_art_h + 3, 1);
        WINDOW* mainWin = newwin(max_y_stdscr - 3, max_x_stdscr - lain_art_w - 4, 1, lain_art_w + 3);
        WINDOW* textWin = newwin(6, max_x_stdscr - lain_art_w - 6, max_y_stdscr - 9, lain_art_w + 4);
        WINDOW* messageWin = newwin(max_y_stdscr - getmaxy(textWin) - 5, max_x_stdscr - lain_art_w - 6, 2, lain_art_w + 4);

        // TextField and Form
        createTextForm(textWin);

        // Stdscr
        box(stdscr, 0, 0);
        refresh();

        // LainWindow
        printLain(lainWin);
        wrefresh(lainWin);

        // SideWindow
        box(sideWin, 0, 0);
        wrefresh(sideWin);

        // MainWindow
        box(mainWin, 0, 0);
        wrefresh(mainWin);

        // TextWindow
        box(textWin, '|', '-');
        wrefresh(textWin);

        // MessageWindow
        box(messageWin, 0, 0);
        wrefresh(messageWin);

        drawHelp();

        // Setting states
        state.lainWin = lainWin;
        state.sideWin = sideWin;
        state.mainWin = mainWin;
        state.textWin = textWin;
        state.messageWin = messageWin;
}

static void deleteUi()
{
        delwin(state.lainWin);
        delwin(state.sideWin);
        delwin(state.mainWin);
        delwin(state.textWin);
        delwin(state.messageWin);
        unpost_form(state.textForm);
	free_form(state.textForm);
	free_field(state.textField[0]);
}

static void createTextForm(WINDOW *win)
{
        int start_x, start_y, x, y;
        getmaxyx(win, y, x);
        getbegyx(win, start_y, start_x);

        state.textField[0] = new_field(y - 2, x - 2, start_y + 1, start_x + 1, 0, 0);
        state.textField[1] = NULL;
        field_opts_off(state.textField[0], O_AUTOSKIP);
        state.textForm = new_form(state.textField);
        post_form(state.textForm);
        form_driver(state.textForm, REQ_INS_MODE);
}

static void drawHelp()
{
        int y = getmaxy(stdscr) - 2;
        for (int x = 1; x < getmaxx(stdscr) - 1; x++) {
                mvaddch(y, x, ' ');
        }

        attron(COLOR_PAIR(4));
        if (state.insertMode) {
                mvprintw(y, 2, "Enter: send message    Shift+Enter: newline    ESC: leave insert mode");
        } else {
                mvprintw(y, 2, "i: enter insert mode    q: exit");
        }
        attroff(COLOR_PAIR(4));
}

// NET
static bool isValidNumber(const char *str) 
{
        for (int i = 0; str[i] != '\0'; i++) {
                if (!isdigit(str[i])) {
                        return false;
                }
        }
        return true;
}

static unsigned short convertPort(const char *port_str) 
{
        if (!isValidNumber(port_str)) {
                fprintf(stderr, RED "Error: Port must be a valid number.\n" CRESET);
                exit(EXIT_FAILURE);
        }

        int port = atoi(port_str);
        if (port < 1 || port > 10000) {
                fprintf(stderr, RED "Error: Port must be between 1 and 10000.\n" CRESET);
                exit(EXIT_FAILURE);
        }

        return (unsigned short)port;
}

static void initConnection(const char* ip, unsigned short port, const char* name)
{
        state.socket = socket(AF_INET, SOCK_STREAM, 0);
        if (state.socket == -1) {
                fprintf(stderr, RED "Socket creation failed! errno: %s\n" CRESET, strerror(errno));
                finish(0);
        }

        struct sockaddr_in serv_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(port),
        };

        if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
                fprintf(stderr, RED "Invalid address\n" CRESET);
                finish(0);
        }

        if (connect(state.socket, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
                fprintf(stderr, RED "Connection failed! errno: %s\n" CRESET, strerror(errno));
                finish(0);
        }

        state.send_buffer = malloc(MAX_BUFFER_SIZE);
        state.recv_buffer = malloc(MAX_BUFFER_SIZE);

        if (!sendMsg(name)) {
                close(state.socket);
                fprintf(stderr, RED "Connection failed! errno: %s\n" CRESET, strerror(errno));
                finish(0);
        }
}

static bool sendMsg(const char* format, ...)
{
        char buffer[MAX_BUFFER_SIZE] = { 0 };

        va_list args;
        va_start(args, format);
        int written = vsnprintf(buffer, MAX_BUFFER_SIZE, format, args);
        va_end(args);

        send(state.socket, buffer, written, 0);
        return true;
}

static void* handleConnection(void* vargp)
{
        int rc;

        while (true) {
                rc = recv(state.socket, state.recv_buffer, MAX_BUFFER_SIZE, 0);
                if (rc == 0) {
                        fprintf(stderr, RED "Connection closed!\n" CRESET);
                        state.isNetThreadAlive = false;
                        finish(0);
                }

                fprintf(stderr, GRN "%d bytes recived.\n" CRESET, rc);
        }

        return NULL; //  Avoid warrning
}
