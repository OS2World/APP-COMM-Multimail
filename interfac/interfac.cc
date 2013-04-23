/*
 * MultiMail offline mail reader
 * Interface, ShadowedWin, ListWindow

 Copyright (c) 1996 Kolossvary Tamas <thomas@tvnet.hu>
 Copyright (c) 1997 John Zero <john@graphisoft.hu>
 Copyright (c) 2007 William McBrine <wmcbrine@users.sf.net>

 Distributed under the GNU General Public License.
 For details, see the file COPYING in the parent directory. */

#include "error.h"
#include "interfac.h"

Interface::Interface()
{
        isoConsole = mm.resourceObject->getInt(Charset);
	lynxNav = mm.resourceObject->getInt(UseLynxNav);
	searchItem = 0;
	goodbye = 0;
#ifdef KEY_RESIZE
	resized = false;
# if defined(SIGWINCH) && !defined(PDCURSES) && !defined(NCURSES_SIGWINCH)
	signal(SIGWINCH, sigwinchHandler);
# endif
#endif
	commandline = abortNow = dontSetAsRead = false;
	unsaved_reply = any_read = false;
	state = nostate;
	width_min = MINWIDTH;
	height_min = mm.resourceObject->getInt(ExpertMode) ?
		MINHIEXPERT : MINHINORM;
}

void Interface::init()
{
	colorlist.Init();
	taglines.Init();
	addresses.Init();
	alive();
	screen_init();
	ansiwindow.Init();
}

void Interface::main()
{
	if (commandline) {
		pktstatus pktret = mm.selectPacket(cmdpktname);
		if (pktret == PKT_OK)
			newpacket();
		else
			fatalError(pkterrmsg(pktret));
	} else {
		packets.init();
		newstate(packetlist);
	}
	KeyHandle();
}

Interface::~Interface()
{
	delete screen;
	touchwin(stdscr);
	refresh();
	leaveok(stdscr, FALSE);
	echo();
	endwin();
}

void Interface::init_colors()
{
#if defined(NCURSES_VERSION) || defined(PDCURSES)
	bool trans = mm.resourceObject->getInt(Transparency);
	int bkcol = PAIR_NUMBER(ColorArray[C_SBACK]) & 7;
#endif
	for (int back = COLOR_BLACK; back <= (COLOR_WHITE); back++)
		for (int fore = COLOR_BLACK; fore <= (COLOR_WHITE); fore++)
			init_pair((fore << 3) + back, fore,
#if defined(NCURSES_VERSION) || defined(PDCURSES)
				(trans && (back == bkcol)) ? -1 :
#endif
				back);

	// Color pair 0 cannot be used (argh!), so swap:

	init_pair(((COLOR_WHITE) << 3) + (COLOR_WHITE),
		COLOR_BLACK, COLOR_BLACK);

	// Of course, now I can't do white-on-white (grey). :-(
}

void Interface::alive()
{
	initscr();
	refresh();

	if (mm.resourceObject->getInt(UseColors))
		start_color();

#if defined(NCURSES_VERSION) || defined(PDCURSES)
	use_default_colors();
#endif
	init_colors();
	cbreak();
	noecho();
	nonl();
	//raw();

#ifdef USE_MOUSE
	if (mm.resourceObject->getInt(Mouse))
		mousemask(BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED |
			BUTTON3_CLICKED, 0);
#endif
}

void Interface::screen_init()
{
	char tmp[80];

	// Make a new background window, fill it with MM_BOARD characters:

	screen = new Win(LINES, COLS, 0, C_SBACK);

	if (mm.resourceObject->getInt(BackFill)) {
		for (int y = 1; y < (LINES - 1); y++)
			for (int x = 1; x < (COLS - 1); x++)
				screen->put(y, x, MM_BOARD);
	}

	if ((COLS < width_min) || (LINES < height_min)) {
		sprintf(tmp, "A screen at least %dx%d is required",
			width_min, height_min);
		fatalError(tmp);
	}

	// Border and title:

#if defined(PDCURSES)
	PDC_set_title(MM_NAME);
#endif
	sprintf(tmp, MM_TOPHEADER, sysname());
	screen->boxtitle(C_SBORDER, tmp, emph(C_SBACK));

	// Help window area:

	if (!mm.resourceObject->getInt(ExpertMode)) {
		screen->attrib(C_SSEPBOTT);
		screen->horizline(LINES - 5);
	}
	screen->delay_update();
}

const char *Interface::pkterrmsg(pktstatus pktret)
{
	return (pktret == PKT_UNFOUND) ?
			"Could not open packet" :
		(pktret == PKT_NOFILES) ?
			"No files uncompresed - check archiver config" :
		(pktret == UNCOMP_FAIL) ?
			"Could not uncompress packet" :
			"Packet type not recognized";
}

int Interface::WarningWindow(const char *warning, const char **selectors,
				int items)
{
	static const char *yesno[] = { "Yes", "No" };
	const char **p;

	int x, z, width, itemlen, c, curitem, def_val = 0;
	bool result = false;

	if (!selectors)
		selectors = yesno;		// default options

	// Calculate the window width and maximum item width:

	for (p = selectors, itemlen = 0, curitem = 0; curitem < items;
	    curitem++, p++) {
		z = strlen(*p);
		if (z > itemlen)
			itemlen = z;
	}
	itemlen += 2;
	x = itemlen * items + 3;
	z = strlen(warning);
	if ((z + 4) > x) {
		x = z + 4;
		itemlen = z / items;
	}
	width = x;

	ShadowedWin warn(7, width, (LINES >> 1) - 4, C_WTEXT);
	warn.put(2, (x - z) >> 1, warning);

	// Display each item:

	for (p = selectors, curitem = 0; curitem < items; curitem++) {
		x = curitem * itemlen + ((itemlen - strlen(*p) + 5) >> 1);

		warn.attrib(C_WTEXT);
		warn.put(4, x + 1, *p + 1);

		warn.attrib(C_WTEXTHI);
		warn.put(4, x, **p++);
	}

	// Main loop -- highlight selected item and process keystrokes:

	while (!result) {
		for (p = selectors, curitem = 0; curitem < items;
		    curitem++, p++) {
			z = strlen(*p);
			x = curitem * itemlen + ((itemlen - z + 5) >> 1);
			warn.put(4, x - 1, (def_val == curitem) ? '[' : ' ');
			warn.put(4, x + z, (def_val == curitem) ? ']' : ' ');
		}
		warn.cursor_on();	// For SIGWINCH
		warn.update();
		warn.cursor_off();

		do
			c = warn.inkey();
		while (ERR == c);

		for (p = selectors, curitem = 0; (curitem < items) &&
		    !result; curitem++, p++)
			if ((c == tolower(**p)) || (c == toupper(**p))) {
				def_val = curitem;
				result = true;
			}

		if (!result)
			switch (c) {
#ifdef USE_MOUSE
			case MM_MOUSE:
				mm_mouse_get();
				if ((LINES >> 1) == mm_mouse_event.y)
				    for (p = selectors, curitem = 0;
					(curitem < items) && !result;
					curitem++, p++) {
					    z = strlen(*p);
					    x = curitem * itemlen +
						((itemlen - z + 5) >> 1) +
						((COLS - width) / 2);
					    if ((x <= mm_mouse_event.x) &&
					     (mm_mouse_event.x <= (x + z))) {
						def_val = curitem;
						result = true;
					    }
				    }
				break;
#endif
			case MM_LEFT:
				if (!def_val)
					def_val = items;
				def_val--;
				break;
			case MM_RIGHT:
			case '\t':
				if (++def_val == items)
					def_val = 0;
				break;
			case MM_ENTER:
				result = true;
			}
	}
	return (items - 1) - def_val;		// the order is inverted
}

int Interface::savePrompt(const char *prompt1, char *filename)
{
	ShadowedWin question(5, 60, (LINES >> 1) - 3, C_LLSAVEBORD);
	question.attrib(C_LLSAVETEXT);
	question.put(1, 2, prompt1);
	question.put(2, 2, "<");
	question.put(2, 57, ">");
	question.put(3, 38, "<ESC> to cancel");

	return question.getstring(2, 3, filename, 54, C_LLSAVETEXT,
		C_LLSAVEGET);
}

void Interface::nonFatalError(const char *warn)
{
	static const char *ok[] = { "OK" };

	WarningWindow(warn, ok, 1);
	redraw();
}

void Interface::ReportWindow(const char *message)
{
	ShadowedWin rwin(3, strlen(message) + 4, (LINES >> 1) - 2,
		C_WTEXT);
	rwin.put(1, 2, message);
	rwin.update();
}

statetype Interface::active()
{
	return state;
}

statetype Interface::prevactive()
{
	return prevstate;
}

void Interface::oldstate(statetype n)
{
	if (n != nostate)
		helpwindow.Delete();

	switch (n) {
	case packetlist:
		packets.Delete();
		break;
	case arealist:
		areas.Delete();
		break;
	case letterlist:
		letters.Delete();
		break;
	case littlearealist:
		areas.Select();
	case address:
	case tagwin:
		currList->Delete();
		oldstate(prevstate);
		break;
	case letter_help:
	case letter:
		letterwindow.Delete();
		break;
	case ansi_help:
	case ansiwin:
		ansiwindow.Delete();
	default:;
	}
}

void Interface::helpreset(statetype oldst)
{
	screen->wtouch();
	if ((oldst != state) && (prevstate != state))
		helpwindow.baseReset();
}

void Interface::newstate(statetype n)
{
	statetype oldst = state;
	state = n;

	switch (n) {
	case packetlist:
		helpreset(oldst);
		currList = &packets;
		packets.MakeActive();
		break;
	case arealist:
		helpreset(oldst);
		currList = &areas;
		areas.MakeActive();
		break;
	case letterlist:
		helpreset(oldst);
		currList = &letters;
		letters.MakeActive();
		break;
	case letter:
		letterwindow.MakeActive(oldst == letterlist);
		break;
	case letter_help:
		letterwindow.MakeActive(false);
		break;
	case littlearealist:
		newstate(prevstate);
		state = littlearealist;
		currList = &littleareas;
		littleareas.MakeActive();
		break;
	case address:
		newstate(prevstate);
		state = address;
		currList = &addresses;
		addresses.MakeActive(addrparm);
		break;
	case tagwin:
		newstate(prevstate);
		state = tagwin;
		currList = &taglines;
		taglines.MakeActive();
		break;
	case ansi_help:
	case ansiwin:
		ansiwindow.MakeActive();
	default:;
	}

	helpwindow.MakeActive();
}

void Interface::changestate(statetype n)
{
	oldstate(state);
	newstate(n);
}

void Interface::redraw()
{
	changestate(state);
}

void Interface::newpacket()
{
	file_header *hello, **bulletins;
	static const char *keepers[] = {"Save", "Kill"};
	unsaved_reply = any_read = false;

	if (mm.checkForReplies())
		if (!WarningWindow("Existing replies found:", keepers))
			mm.deleteReplies();
		else {
			redraw();
			ReportWindow("Opening replies...");
		}

	mm.openReply();

	mm.areaList = new area_list(&mm);
	int noOfReplies = mm.areaList->getRepList();
	mm.driverList->initRead();
	if (mm.getOffConfig() || noOfReplies) {
		mm.areaList->setMode(0);
		mm.areaList->relist();
	}

	bool latin = mm.packet->isLatin();

	hello = mm.packet->getHello();
	goodbye = mm.packet->getGoodbye();
	bulletins = mm.packet->getBulletins();

	if (hello)
		ansiFile(hello, hello->getName(), latin);

	if (!abortNow) {
		areas.FirstUnread();
		changestate(arealist);
	}

	if (!abortNow && bulletins) {
		if (WarningWindow("View bulletins / new files?"))
			ansiList(bulletins, latin);
		else
			redraw();
	}
}

bool Interface::select()
{
	pktstatus pktret;

	switch (state) {
	case packetlist:
		ReportWindow("Opening...");

		pktret = packets.OpenPacket();

		if (pktret == PKT_OK)
			newpacket();
		else
			if (pktret != NEW_DIR)
				nonFatalError(pkterrmsg(pktret));
		break;
	case arealist:
		areas.Select();
		if (mm.areaList->getNoOfLetters() > 0) {
			ReportWindow("Opening...");

			mm.areaList->getLetterList();
			letters.FirstUnread();
			changestate(letterlist);
		}
		break;
	case letterlist:
		letters.Select();
		changestate(letter);
		break;
	case letter:
		letterwindow.Next();
		break;
	case littlearealist:
	case address:
		currList->KeyHandle('\n');
	case tagwin:
		Key = '\n';
	case ansiwin:
	case ansi_help:
	case letter_help:
		return back();
	default:;
	}
	return false;
}

bool Interface::back()
{
	switch (state) {
	case packetlist:
		switch (Key) {
#ifdef USE_MOUSE
			case MM_MOUSE:
#endif
			case MM_LEFT:
				if (!packets.back())
					return false;
		}
		if (abortNow || WarningWindow("Do you really want to quit?")) {
			oldstate(state);
			return true;
		} else
			redraw();
		break;
	case arealist:
		if (any_read)
			save_read();
		if (unsaved_reply)
			if (mm.resourceObject->getInt(AutoSaveReplies) ||
				WarningWindow(
			"The REPLY area has changed. Save changes?")) {
				redraw();
				create_reply_packet();
			}
		if (!abortNow && goodbye) {
			ansiFile(goodbye, goodbye->getName(),
				mm.packet->isLatin());
			goodbye = 0;
		}
		mm.Delete();
		if (abortNow || commandline) {
			oldstate(state);
			return true;
		} else
			changestate(packetlist);
		break;
	case letterlist:
		delete mm.letterList;
	case letter:
	case letter_help:
	case ansi_help:
		changestate((statetype)(state - 1));
		if (abortNow)
			return back();
		break;
	case littlearealist:
	case address:
	case tagwin:
	case ansiwin:
		changestate(prevstate);
		return (abortNow && (state != arealist)) ? back() : true;
	default:;
	}
	return false;
}

#ifdef KEY_RESIZE
void Interface::sigwinch()
{
	oldstate(state);

	delete screen;
# ifdef PDCURSES
	resize_term(0, 0);
# else
#  if defined(SIGWINCH) && !defined(NCURSES_SIGWINCH)
	endwin();
	initscr();
	refresh();
#  endif
# endif
	screen_init();
	newstate(state);
	resized = false;
}
#endif

void Interface::kill_letter()
{
	if (WarningWindow(
		"Are you sure you want to delete this letter?")) {

		redraw();
		mm.areaList->killLetter(mm.letterList->getAreaID(),
			mm.letterList->getMsgNum());
		setUnsaved();

		changestate(letterlist);
		if (!mm.areaList->getNoOfLetters())
			back();
	} else
		redraw();
}

void Interface::create_reply_packet()
{
	static int lines = mm.resourceObject->getInt(MaxLines);
	if (lines)
		letterwindow.SplitAll(lines);

	ReportWindow("Saving replies...");
	bool result = mm.makeReply();

	if (result)
		unsaved_reply = false;
	else
		nonFatalError("Warning: Unable to create reply packet!");
}

void Interface::save_read()
{
	ReportWindow("Saving last read...");
	if (mm.saveRead())
		any_read = false;
	else {
		redraw();
		nonFatalError("Could not save last read");
	}
}

void Interface::addressbook(bool NoEnter)
{
	prevstate = state;
	addrparm = NoEnter;
	changestate(address);
	KeyHandle();
}

bool Interface::Tagwin()
{
	prevstate = state;
	changestate(tagwin);
	KeyHandle();
	switch (Key) {
	case MM_ENTER:
		return true;
	}
	return false;
}

int Interface::ansiLoop(letter_body *source, const char *title, bool latin)
{
	ansiwindow.set(source, title, latin);
	return ansiCommon();
}

int Interface::ansiFile(file_header *f, const char *title, bool latin)
{
	ansiwindow.set(f, title, latin);
	return ansiCommon();
}

void Interface::ansiList(file_header **base, bool latin)
{
	file_header **a = base;

	while (a && *a) {
		switch (ansiFile(*a, (*a)->getName(), latin)) {
		case 1:
			a++;
			break;
		case -1:
			if (a != base)
				a--;
			break;
		default:
			a = 0;
		}
	}
}

int Interface::ansiCommon()
{
	prevstate = state;
	changestate(ansiwin);
	KeyHandle();
	switch (Key) {
	case MM_LEFT:
		if (lynxNav)
			return 0;
	case MM_MINUS:
		return -1;
	case MM_RIGHT:
	case MM_ENTER:
	case MM_PLUS:
		return 1;
	}
	return 0;
}

int Interface::areaMenu()
{
	prevstate = state;
	littleareas.init();
	changestate(littlearealist);
	KeyHandle();
	return littleareas.getArea();
}

void Interface::setUnsaved()
{
	unsaved_reply = true;
	if (mm.resourceObject->getInt(AutoSaveReplies))
		create_reply_packet();
}

void Interface::setUnsavedNoAuto()
{
	unsaved_reply = true;
}

void Interface::setAnyRead()
{
	any_read = true;
}

bool Interface::dontRead()
{
	return dontSetAsRead;
}

void Interface::searchNext()
{
	if (searchItem) {
		// We should only continue if the search was started in an
		// appropriate state with respect to the current state.

		bool stateok;

		switch (state) {
		case letter:
		case letterlist:
		case arealist:
			stateok = ((int) state >= (int) searchstate);
			break;
		default:
			stateok = (state == searchstate);
		}

		if (stateok) {
			searchret result;

			dontSetAsRead = true;

			bool restorepos = (s_oldpos == -1);

			ReportWindow("Searching (ESC to abort)...");

			switch (state) {
			case letter:
				if (restorepos) {
					s_oldpos = letterwindow.getPos();
					letterwindow.setPos(-1);
				}
				result = letterwindow.search(searchItem);
				if ((result != True) && restorepos)
					letterwindow.setPos(s_oldpos);
				break;
			case ansiwin:
				if (restorepos) {
					s_oldpos = ansiwindow.getPos();
					ansiwindow.setPos(-1);
				}
				result = ansiwindow.search(searchItem);
				if ((result != True) && restorepos)
					ansiwindow.setPos(s_oldpos);
				break;
			default:
				if (restorepos) {
					s_oldpos = currList->getActive();
					currList->setActive(-1);
				}
				result = currList->search(searchItem,
					searchmode);
				if ((result != True) && restorepos)
					currList->setActive(s_oldpos);
			}

			dontSetAsRead = false;

			if (result == False)
				if (state == searchstate) {
					redraw();
					nonFatalError("No more matches");
				} else {
					if (state == letterlist)
						delete mm.letterList;
					else
						if (state == arealist)
							mm.Delete();
					changestate((statetype)
						((int) state - 1));
					searchNext();
				}
			else
				if (result == Abort) {
					if (state == searchstate)
						redraw();
					else
						while (state != searchstate)
						    changestate((statetype)
							((int) state - 1));
					nonFatalError("Search aborted");
				} else
					redraw();
		}
	}
}

void Interface::searchSet()
{
	static const char *searchopts[] = { "Pkt list", "Area list",
		"Headers", "Full text", "Quit" };
	static char item[80];

	bool inPackets = (state == packetlist);
	bool hasAreas = (state == arealist) || inPackets;
	if (hasAreas || (state == letterlist)) {
	        searchmode = WarningWindow("Search which?", searchopts +
        	        !hasAreas + !inPackets, 3 + hasAreas + inPackets);

		if (!searchmode) {
			searchItem = 0;
			return;
		}
	}
	if (!savePrompt("Search for:", item))
		*item = '\0';

	searchItem = *item ? item : 0;
	searchstate = state;
	s_oldpos = -1;
}

void Interface::setKey(int newkey)
{
	ungetch(newkey);
}

bool Interface::fromCommandLine(const char *pktname)
{
	mychdir(error.getOrigDir());

	commandline = mychdir(pktname);

	if (!commandline) {
		char *cdir = mygetcwd();
		mm.resourceObject->set_noalloc(PacketDir, cdir);

		main();

		abortNow = true;
	} else {
		char *s, *t = strpbrk((char *) pktname, "/\\");

		if (!t) {
			s = new char[strlen(pktname) + 3];
			sprintf(s, "./%s", pktname);
		} else
			s = strdupplus(pktname);

		cmdpktname = s;

		main();

		delete[] s;
		state = nostate;
	}
	return commandline;
}

void Interface::KeyHandle()		// Main loop
{
	bool end = false;

	while (!(end || abortNow)) {
#ifdef KEY_RESIZE
		if (resized)
			sigwinch();
#endif
		doupdate();
		Key = screen->inkey();
#ifdef KEY_RESIZE
		resized = (KEY_RESIZE == Key);
#endif
		if (((state == letter_help) || (state == ansi_help))
#ifdef KEY_RESIZE
			&& !resized
#endif
			) {
				if (ERR != Key) {
					abortNow = (Key == 24);
					back();
				}
		} else {
			if ((Key >= 'a') && (Key <= 'z'))
				Key = toupper(Key);

			switch (Key) {
			case 24:		// ^X
				abortNow = true;
			case 'Q':
			case MM_ESC:
			case MM_BACKSP:
				end = back();
				break;
			case MM_ENTER:
				end = select();
				break;
#ifdef USE_SHELL
			case 26:		// ^Z
				shell.out();
				break;
#endif
			case MM_SLASH:
				searchSet();
				redraw();
			case '.':
			case '>':
				searchNext();
				break;
			case 'A':
				switch (state) {
				case packetlist:
				case arealist:
				case letterlist:
				case letter:
					addressbook(true);
					break;
				case address:
				case tagwin:
					currList->KeyHandle(Key);
					break;
				case ansiwin:
					ansiwindow.KeyHandle(Key);
				default:;
				}
				break;
			case 20:		// ^T
				switch (state) {
				case packetlist:
				case arealist:
				case letterlist:
				case letter:
					Tagwin();
				default:;
				}
				break;
			case 'C':
				isoConsole = !isoConsole;
				redraw();
				break;
			case 'O':
				switch (state) {
				case packetlist:
				case arealist:
				case letterlist:
					helpwindow.baseNext();
					break;
				case letter:
					letterwindow.KeyHandle(Key);
				default:;
				}
				break;
			case '!':
			case MM_F2:
				switch (state) {
				case arealist:
				case letterlist:
				case letter:
					if (!mm.checkForReplies() ||
					     WarningWindow(
	"This will overwrite the existing reply packet. Continue?")) {
						redraw();
						create_reply_packet();
					}
					redraw();
				default:;
				}
				break;
#ifdef USE_MOUSE
			case MM_MOUSE:
				mm_mouse_get();
				if (mm_mouse_event.bstate & BUTTON3_CLICKED) {
					end = back();
					break;
				}
#endif
			default:
				switch (state) {
				case letter:
					letterwindow.KeyHandle(Key);
					break;
				case ansiwin:
					switch (Key) {
					case MM_RIGHT:
					case MM_LEFT:
					case MM_PLUS:
					case MM_MINUS:
						end = back();
						break;
					default:
						ansiwindow.KeyHandle(Key);
					}
					break;
				default:
					end = currList->KeyHandle(Key);
				}
			}
		}
	}
}
