/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

/*** defines***/

#define CTRL_KEY(k) ((k) & 0x1f)

int CMD_CHAR = 0x1;
int CMD_CTRL = 0x2;
int CMD_AROW = 0x4;

/*** data ***/

struct termios pred_term;

typedef struct bfg_char bfg_char;
typedef struct cmd cmd;
typedef struct coor coor;

struct bfg_char {
	int x;
	int y;
	char c;
	bfg_char *next;
};

struct cmd {
	int cmd_type;
	char c;
};;

struct coor{
	int x;
	int y;
};

/*** terminal ***/

void unSlam();
void clear_screen();

void editor_error(char *s) {
	unSlam();
	//clear_screen();
	printf("Upper level editor error:\n%s\n", s);
	exit(1);
}

void unSlam() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &pred_term) == -1) editor_error("tcsetattr failed");
}

void ham_jam_slam_it() {
	// convert terminal to raw mode
	if (tcgetattr(STDIN_FILENO, &pred_term) == -1) editor_error("tcgetattr failed");
	atexit(unSlam);
	struct termios raw = pred_term;
	raw.c_iflag &= ~(IXON | ICRNL | BRKINT | ISTRIP | INPCK);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) editor_error("tcsetattr failed");
}

void clear_screen() {
	// Clear the terminal and position the cursor at the top left
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

int get_cursor_position(coor *curs) {
	char buf[32];
	unsigned int i = 0;
	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;
	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}
	buf[i] = '\0';

	int x, y;
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", &y, &x) != 2) return -1;
	curs->x = x;
	curs->y = y;
	return 0;
}

int get_terminal_size(coor *s) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return get_cursor_position(s);
	}
	else {
		s->x = ws.ws_col;
		s->y = ws.ws_row;
		return 0;
	}
}

int draw_screen(char *b, coor cursor_pos, coor screen_size) {
	// Takes a write buffer, refreshes the screen, writes the new screen,
	// and moves the cursor to the desired locaton.
	// For now, for safety the fuction checks if the buffer has exactly
	// enough characters to fill the screen - no tabs or newlines!
	// Remember to null terminate the buffer
	int i = 0;
	while (*(b + i)) {
		i++;
	}
	if (i != screen_size.x * screen_size.y) return -1;
	clear_screen();
	write(STDOUT_FILENO, b, i);

	if (!(1 <= cursor_pos.x <= screen_size.x)) editor_error("out of bounds cursor in draw_screen");
	if (!(1 <= cursor_pos.y <= screen_size.y)) editor_error("out of bounds cursor in draw_screen");
	char *curs_seq = (char *) malloc(11 * sizeof (char));
	sprintf(curs_seq, "\x1b[%03d;%03dH", cursor_pos.y, cursor_pos.x);
	write(STDOUT_FILENO, curs_seq, 10);
	free(curs_seq);

	return 0;
}

/*** keyhandling **/

cmd read_key() {
	cmd res = {0, '\0'};
	char c = '\0';
	if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) editor_error("keystroke read");

	if (!iscntrl(c)) {
		res.cmd_type |= CMD_CHAR;
		res.c = c;
		return res;
	}

	cmd esc = {0, 'e'};

	if (c == '\x1b') {
		char seq[3];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return esc;
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return esc;
		if (seq[0] == '[') {
			int tp = 0 | CMD_AROW;
			switch (seq[1]) {
				case 'A': return (cmd) {tp, '^'};
				case 'B': return (cmd) {tp, 'v'};
				case 'C': return (cmd) {tp, '>'};
				case 'D': return (cmd) {tp, '<'};
			}
		}
		return esc;
	}

	if (!c) return res;

	// if execution reaches here the key should be a ctrl+key combination

	res = (cmd) {CMD_CTRL, c + 'a' - 1};

	return res;
}

/*** beforge-specific ***/

coor add_coor(coor a, coor b) {
	// Does what it says
	coor res = {0, 0};
	res.x = a.x + b.x;
	res.y = a.y + b.y;
	return res;
}

coor subtract_coor(coor a, coor b) {
	// Does what it says
	coor res = {0, 0};
	res.x = a.x - b.x;
	res.y = a.y - b.y;
	return res;
}

coor max_xy(bfg_char *clist) {
	int maxx = 1;
	int maxy = 1;
	while(clist->next) {
		if (clist->c != ' ') {
			if (clist->x > maxx) maxx = clist->x;
			if (clist->y > maxy) maxy = clist->y;
		}
		clist = clist->next;
	}
	if (clist->c != ' ') {
		if (clist->x > maxx) maxx = clist->x;
		if (clist->y > maxx) maxy = clist->y;
	}
	return (coor) {maxx, maxy};
}

int list_length(bfg_char *list) {
	int i = 0;
	while (list) {
		list = list->next;
		i++;
	}
	return i;
}

void add_char(char c, coor loc, bfg_char *listbegin) {
	bfg_char *bc = listbegin;
	while (bc->next) {
		bc = bc->next;
	}
	bc->next = malloc(sizeof (bfg_char));
	bc = bc->next;
	*bc = (bfg_char) {loc.x, loc.y, c, NULL};
}

char find_char(int x, int y, bfg_char *listbegin) {
	// Used for both final file write and for displaying
	bfg_char *bc = listbegin;
	char c = '\0';
	while (bc->next) {
		if ((bc->x == x) && (bc->y == y)) {
			c = bc->c;
		}
		bc = bc->next;
	}
	if ((bc->x == x) && (bc->y == y)) {
		c = bc->c;
	}
	return c; // latest write over a location is final value
}

void set_body(char *buf, bfg_char *clist, coor tsize, coor offset) {
	// Draws across the whole screen - set_closer will overwrite bottom two lines if used
	int adj_x, adj_y, bufloc;
	char c;
	for (int x = 1; x <= tsize.x; x++) {
		for (int y = 1; y <= tsize.y; y++) {
			bufloc = (x-1) + tsize.x * (y-1); // location of that coordinate in the buffer

			adj_x = x + offset.x;
			adj_y = y + offset.y;

			c = find_char(adj_x, adj_y, clist);
			if (c && !iscntrl(c)) {
				buf[bufloc] = c;
			}
			else {
				buf[bufloc] = ' ';
			}
		}
	}
}

void set_closer(char *buf, coor tsize, char mode, char* msg) {
	// Set the final two lines of the screen
	for (int i = tsize.x * (tsize.y - 2); i < tsize.x * (tsize.y - 1); i++) {
		buf[i] = '_';
	}
	for (int i = tsize.x * (tsize.y - 1); i < tsize.x * (tsize.y); i++) {
		buf[i] = ' ';
	}
	buf[tsize.x * tsize.y] = '\0';
	char *bottom_text = malloc((256) * sizeof (char));
	switch (mode) {
		case 'e':
			sprintf(bottom_text, "BEFORGE -- Editing %s", msg);
			break;
		case 'x':
			sprintf(bottom_text, "BEFORGE -- $ %s", msg);
			break;
	}
	int i = 0;
	while (bottom_text[i] && buf[tsize.x * (tsize.y - 1) + i]) {
		buf[tsize.x * (tsize.y - 1) + i] = bottom_text[i];
		i++;
	}
}

void set_screen(char *buf, coor tsize, char mode, bfg_char *clist, coor offset, char *msg) {
	set_body(buf, clist, tsize, offset);
	set_closer(buf, tsize, mode, msg);
}

void handle_edit_key(char c, bfg_char *clist, coor *curpos, coor *curv, int string_mode) {
	// add to clist, change cursor positions and velocities in place. Cursor position overhang will be handled separately.
	// cursor position only changed by a '#' to jump 2 without changing v. Other cursor movement handled by 
	// move_cursor(), which also takes care of frame movement and document size changes
	// Woo side effects!

	add_char(c, *curpos, clist);

	if (!string_mode) {
		switch (c) {
			case '#':
				*curpos = add_coor(*curpos, *curv);
				break;
			case '|':
				*curv = (coor) {0, 0};
				break;
			case '_':
				*curv = (coor) {0, 0};
				break;
			case '>':
				*curv = (coor) {1, 0};
				break;
			case 'v':
				*curv = (coor) {0, 1};
				break;
			case '<':
				*curv = (coor) {-1, 0};
				break;
			case '^':
				*curv = (coor) {0, -1};
				break;
		}
	}
}

void handle_arrow_key(char c, coor *curv) {
	switch (c) {
		case '>':
			*curv = (coor) {1, 0};
			break;
		case 'v':
			*curv = (coor) {0, 1};
			break;
		case '<':
			*curv = (coor) {-1, 0};
			break;
		case '^':
			*curv = (coor) {0, -1};
			break;
	}
}

void move_cursor(coor *curpos, coor *curv, coor *dsize, coor *offset, coor tsize) {
	// add velocity on to curpos and do the necessary changes to docsize and offset
	// for everything to fit onto the screen (remember bottom 2 rows are reserved)
	// after v addition the cursor could have overshot by more than 1 bc of #

	*curpos = add_coor(*curpos, *curv);

	// expand document size if necessary
	if (curpos->x > dsize->x) {
		dsize->x = curpos->x;
	}
	if (curpos->y > dsize->y) {
		dsize->y = curpos->y;
	}

	// if the cursor is in <=0 x or y return to 1 in that dimension (and set v in that dimension to 0)
	if (curpos->x <= 0) {
		curpos->x = 1;
		curv->x = 0;
	}
	if (curpos->y <= 0) {
		curpos->y = 1;
		curv->y = 0;
	}

	coor pos_in_terminal = subtract_coor(*curpos, *offset);
	// if the adjusted cursor position is greater than the terminal size, increase offset accordingly
	// if pos_in_terminal is <1, reduce offset accordingly (absolute curpos < 1 is already handled)
	if (pos_in_terminal.x > tsize.x) {
		offset->x += (pos_in_terminal.x - tsize.x);
	}
	else if (pos_in_terminal.x < 1) {
		offset->x += (pos_in_terminal.x - 1);
	}

	if (pos_in_terminal.y > (tsize.y - 2)) {
		offset->y += (pos_in_terminal.y - (tsize.y - 2));
	}
	else if (pos_in_terminal.y < 1) {
		offset->y += (pos_in_terminal.y - 1);
	}
}

char* export_doc(bfg_char *char_list) {
	coor maxvals = max_xy(char_list);
	char c;

	char *out = (char *) malloc(((maxvals.x * maxvals.y) + maxvals.y) * sizeof (char)); // x by y characters plus (y - 1) newlines plus a null terminator
	int i = 0;

	for (int y = 1; y <= maxvals.y; y++) {
		for (int x = 1; x <= maxvals.x; x++) {
			c = find_char(x, y, char_list);
			if (!c) c = ' ';
			out[i] = c;
			i++;
		}
		if (y != maxvals.y) {
			out[i] = '\n';
			i++;
		}
	}
	out[i] = '\0';
	return out;
}

/*** init ***/

int main() {
	ham_jam_slam_it();
	char mode = 'e'; // e for edit
	cmd keystroke;
	int ex = 0;

	coor doc_size = {1, 1};
	coor doc_offset = {0, 0}; // offset will always be >= 0. For now editing is only allowed below and to the right of the origin
	coor cursor_pos = {1, 1}; // To be clear curpos is 1-indexed position *within the document*, not the location to draw the cursor
	coor cursor_v = {1, 0};
	coor adjusted_cursor_pos;
	bfg_char *charlist = malloc(sizeof (bfg_char));
	*charlist = (bfg_char) {-1, -1, ' ', NULL};

	coor terminal_size;
	coor new_size;
	if (get_terminal_size(&terminal_size)) editor_error("Could not get terminal size");
	char *writebuf = (char *) malloc((terminal_size.x * terminal_size.y + terminal_size.y) * sizeof (char));

	set_screen(writebuf, terminal_size, mode, charlist, doc_offset, "");
	adjusted_cursor_pos = subtract_coor(cursor_pos, doc_offset);
	draw_screen(writebuf, adjusted_cursor_pos, terminal_size);

	char message[256]; // For displaying messages on the bottom of the window
	sprintf(message, "");
	char saveloc[256];
	char saveloc_i;

	int string_mode = 0;

	char cmd_buffer[256];

	while (1) {

		keystroke = read_key();

		if (get_terminal_size(&new_size)) editor_error("Could not get terminal size");
		if (new_size.x != terminal_size.x || new_size.y != terminal_size.y) {
			terminal_size.x = new_size.x;
			terminal_size.y = new_size.y;
			writebuf = (char *) realloc(writebuf, (terminal_size.x * terminal_size.y + terminal_size.y) * sizeof (char));

			// rewrite the screen when window size changes
			set_screen(writebuf, terminal_size, mode, charlist, doc_offset, ""); // formats a screen and sticks it in writebuf
			adjusted_cursor_pos = subtract_coor(cursor_pos, doc_offset);
			draw_screen(writebuf, adjusted_cursor_pos, terminal_size);
		}

		if (keystroke.cmd_type) {
			// keystroke handling function handling
			switch (mode) {
				case 'e':
					//if (!iscntrl(keystroke.c)) {
					//	printf("%c\r\n", keystroke.c);
					//}
					if ((keystroke.cmd_type & CMD_CTRL) && (keystroke.c == 'x')) mode = 'x';
					if ((keystroke.cmd_type & CMD_CHAR) && (keystroke.c == '"')) string_mode = !string_mode;
					else if (keystroke.cmd_type & CMD_CHAR) {
						handle_edit_key(keystroke.c, charlist, &cursor_pos, &cursor_v, string_mode);
						move_cursor(&cursor_pos, &cursor_v, &doc_size, &doc_offset, terminal_size);
					}
					else if (keystroke.cmd_type & CMD_AROW) {
						handle_arrow_key(keystroke.c, &cursor_v);
						move_cursor(&cursor_pos, &cursor_v, &doc_size, &doc_offset, terminal_size);
					}
					sprintf(message, "Document size (including overwritten chars): %d", list_length(charlist)); 
					break;
				case 'x':
					//message = "";
					if ((keystroke.cmd_type & CMD_CHAR) && (keystroke.c == 'q')) ex = 1;
					if ((keystroke.cmd_type & CMD_CHAR) && (keystroke.c == 'e')) mode = 'e';
					if ((keystroke.cmd_type & CMD_CHAR) && (keystroke.c == 's')) {
						FILE *fp = fopen("out.b98", "w");
						fprintf(fp, "%s", export_doc(charlist));
						fclose(fp);
						mode = 'e';
					}
					if ((keystroke.cmd_type & CMD_CHAR) && (keystroke.c == 'd')) {
						for (int i = 0; i < terminal_size.x * terminal_size.y; i++) {
							writebuf[i] = 'X';
						}
						writebuf[terminal_size.x * terminal_size.y] = '\0';
						if (draw_screen(writebuf, (coor) {10, 20}, terminal_size)) {
							editor_error("Draw screen failed.");
						}
					}
					break;
			}

			set_screen(writebuf, terminal_size, mode, charlist, doc_offset, message); // formats a screen and sticks it in writebuf
			adjusted_cursor_pos = subtract_coor(cursor_pos, doc_offset);
			draw_screen(writebuf, adjusted_cursor_pos, terminal_size);
		}

		if (ex) break;
	}
	clear_screen();
	return 0;
}