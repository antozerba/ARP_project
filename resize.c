// resize_window.c
#define _XOPEN_SOURCE_EXTENDED
#include <locale.h>
#include <ncurses.h>

static void layout_and_draw(WINDOW *win) {
    int H, W;
    getmaxyx(stdscr, H, W);

    // Finestra con un margine fisso intorno
    int wh = (H > 6) ? H - 6 : H;
    int ww = (W > 10) ? W - 10 : W;
    if (wh < 3) wh = 3;


    // Ridimensiona e ricentra la finestra
    wresize(win, wh, ww);
    mvwin(win, (H - wh) / 2, (W - ww) / 2);

    // Pulisci e ridisegna
    werase(stdscr);
    werase(win);
    box(win, 0, 0);
    mvprintw(0, 0, "Ridimensiona la shell; premi 'q' per uscire.");
    mvwprintw(win, 1, 2, "stdscr: %dx%d  win: %dx%d",
              H, W, wh, ww);

    refresh();
    wrefresh(win);
}

int main(void) {
    setlocale(LC_ALL, "");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);     // necessario per ricevere KEY_RESIZE
    // (opz.) set_escdelay(25);

    // Crea una finestra iniziale (dimensioni provvisorie)
    WINDOW *win = newwin(3, 3, 0, 0);
    layout_and_draw(win);

    int ch;
    while ((ch = getch()) != 'q') {
        if (ch == KEY_RESIZE) {
            // Aggiorna le strutture interne di ncurses per la nuova dimensione
            resize_term(0, 0);          // o resizeterm(0, 0)
            layout_and_draw(win);       // ricalcola layout e ridisegna
        }
        // ... gestisci altri tasti qui ...
    }

    delwin(win);
    endwin();
    return 0;
}
