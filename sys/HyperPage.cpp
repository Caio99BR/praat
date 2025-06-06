/* HyperPage.cpp
 *
 * Copyright (C) 1996-2024 Paul Boersma
 *
 * This code is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This code is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this work. If not, see <http://www.gnu.org/licenses/>.
 */

#include "HyperPage.h"
#include "Printer.h"
#include "machine.h"
#include "GuiP.h"

#include "praat.h"
#include "EditorM.h"

Thing_implement (HyperPage, Editor, 0);

#include "Prefs_define.h"
#include "HyperPage_prefs.h"
#include "Prefs_install.h"
#include "HyperPage_prefs.h"
#include "Prefs_copyToInstance.h"
#include "HyperPage_prefs.h"

#define PAGE_HEIGHT  320.0
#define SCREEN_HEIGHT  18.0
#define PAPER_TOP  12.0
#define TOP_MARGIN  0.8
#define PAPER_BOTTOM  (13.0 - (double) thePrinter. paperHeight / thePrinter. resolution)
#define BOTTOM_MARGIN  0.5
static double resolution;

/********** class HyperLink **********/

Thing_implement (HyperLink, Daata, 0);

autoHyperLink HyperLink_create (conststring32 name, double x1DC, double x2DC, double y1DC, double y2DC) {
	autoHyperLink me = Thing_new (HyperLink);
	Thing_setName (me.get(), name);
	my x1DC = x1DC, my x2DC = x2DC, my y1DC = y1DC, my y2DC = y2DC;
	return me;
}

static void saveHistory (HyperPage me, conststring32 title) {
	if (! title)
		return;

	/*
		The page title will be saved at the top. Go there.
	*/
	while (my historyPointer < 19 && my history [my historyPointer]. page)
		my historyPointer ++;

	/*
		If the page title to be saved is already at the top, ignore it.
	*/
	if (my history [my historyPointer]. page) {
		if (str32equ (my history [my historyPointer]. page.get(), title)) return;
	} else if (my historyPointer > 0 && str32equ (my history [my historyPointer - 1]. page.get(), title)) {
		my historyPointer --;
		return;
	}

	/*
		If the history buffer is full, shift it.
	*/
	if (my historyPointer == 19 && my history [my historyPointer]. page) {
		for (int i = 0; i < 19; i ++)
			my history [i] = std::move (my history [i + 1]);
		my history [19]. page. reset();
	}

	/*
		Add the page title to the top of the history list.
	*/
	my history [my historyPointer]. page = Melder_dup_f (title);
}

/********************************************************************************
 *
 * Before drawing or printing.
 *
 */

void HyperPage_initSheetOfPaper (HyperPage me) {
	int reflect = my mirror && (my d_printingPageNumber & 1) == 0;
	conststring32 leftHeader = reflect ? my outsideHeader : my insideHeader;
	conststring32 rightHeader = reflect ? my insideHeader : my outsideHeader;
	conststring32 leftFooter = reflect ? my outsideFooter : my insideFooter;
	conststring32 rightFooter = reflect ? my insideFooter : my outsideFooter;

	my d_y = PAPER_TOP - TOP_MARGIN;
	my d_x = 0.0;
	my previousBottomSpacing = 0.0;
	Graphics_setFont (my ps, kGraphics_font::TIMES);
	Graphics_setFontSize (my ps, 12);
	Graphics_setFontStyle (my ps, Graphics_ITALIC);
	if (leftHeader) {
		Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_TOP);
		Graphics_text (my ps, 0.7, PAPER_TOP, leftHeader);
	}
	if (my middleHeader) {
		Graphics_setTextAlignment (my ps, Graphics_CENTRE, Graphics_TOP);
		Graphics_text (my ps, 0.7 + 3, PAPER_TOP, my middleHeader);
	}
	if (rightHeader) {
		Graphics_setTextAlignment (my ps, Graphics_RIGHT, Graphics_TOP);
		Graphics_text (my ps, 0.7 + 6, PAPER_TOP, rightHeader);
	}
	if (leftFooter) {
		Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_BOTTOM);
		Graphics_text (my ps, 0.7, PAPER_BOTTOM, leftFooter);
	}
	if (my middleFooter) {
		Graphics_setTextAlignment (my ps, Graphics_CENTRE, Graphics_BOTTOM);
		Graphics_text (my ps, 0.7 + 3, PAPER_BOTTOM, my middleFooter);
	}
	if (rightFooter) {
		Graphics_setTextAlignment (my ps, Graphics_RIGHT, Graphics_BOTTOM);
		Graphics_text (my ps, 0.7 + 6, PAPER_BOTTOM, rightFooter);
	}
	Graphics_setFontStyle (my ps, Graphics_NORMAL);
	if (my d_printingPageNumber)
		Graphics_text (my ps, 0.7 + ( reflect ? 0 : 6 ), PAPER_BOTTOM, my d_printingPageNumber);
	Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_BOTTOM);
}

static void updateVerticalScrollBar (HyperPage me);

void HyperPage_any (HyperPage me, conststring32 text, kGraphics_font font, double size, int style, double minFooterDistance,
	double x, double secondIndent, double topSpacing, double bottomSpacing, uint32 method)
{
	if (my rightMargin == 0.0)
		return;   // no infinite heights please
	const double heightGuess = size * (1.2/72) * ((integer) size * Melder_length (text) / (int) (my rightMargin * 150));

	if (! my printing) {
		Graphics_Link *paragraphLinks;
		int numberOfParagraphLinks, ilink;
		if (my entryHint && (method & HyperPage_USE_ENTRY_HINT) && str32equ (text, my entryHint.get()))
			my entryPosition = my d_y;
		my d_y -= ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0;
		my d_y -= size * (1.2/72);
		my d_x = x;

		if (/* my d_y > PAGE_HEIGHT + 2.0 + heightGuess || */ my d_y < PAGE_HEIGHT - SCREEN_HEIGHT) {
			my d_y -= heightGuess;
		} else {
			Graphics_setFont (my graphics.get(), font);
			Graphics_setFontSize (my graphics.get(), size);
			//if (method & HyperPage_EXECUTABLE)
			//	Graphics_setColour (my graphics.get(), Melder_GREEN);
			//else
			//	Graphics_setColour (my graphics.get(), Melder_BLACK);
			Graphics_setWrapWidth (my graphics.get(), my rightMargin - x - 0.1);
			Graphics_setSecondIndent (my graphics.get(), secondIndent);
			Graphics_setFontStyle (my graphics.get(), style);
			Graphics_text (my graphics.get(), my d_x, my d_y, text);
			numberOfParagraphLinks = Graphics_getLinks (& paragraphLinks);
			for (ilink = 1; ilink <= numberOfParagraphLinks; ilink ++) {
				autoHyperLink link = HyperLink_create (paragraphLinks [ilink]. name,
					paragraphLinks [ilink]. x1, paragraphLinks [ilink]. x2,
					paragraphLinks [ilink]. y1, paragraphLinks [ilink]. y2
				);
				my links. addItem_move (link.move());
			}
			if (method & HyperPage_ADD_BORDER) {
				Graphics_setLineWidth (my graphics.get(), 2.0);
				Graphics_line (my graphics.get(), 0.0, my d_y, my rightMargin, my d_y);
				Graphics_setLineWidth (my graphics.get(), 1.0);
			}
			/*
				The text may have wrapped.
				Ask the Graphics manager by how much, and update our text position accordingly.
			*/
			my d_y = Graphics_inqTextY (my graphics.get());
		}
	} else {
		Graphics_setFont (my ps, font);
		Graphics_setFontSize (my ps, size);
		my d_y -= ( my d_y == PAPER_TOP - TOP_MARGIN ? 0 : ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0 );
		my d_y -= size * (1.2/72);
		if (my d_y < PAPER_BOTTOM + BOTTOM_MARGIN + minFooterDistance + size * (1.2/72) * (Melder_length (text) / (6.0 * 10))) {
			Graphics_nextSheetOfPaper (my ps);
			if (my d_printingPageNumber != 0)
				my d_printingPageNumber ++;
			HyperPage_initSheetOfPaper (me);
			Graphics_setFont (my ps, font);
			Graphics_setFontSize (my ps, size);
			my d_y -= size * (1.2/72);
		}
		my d_x = 0.7 + x;
		Graphics_setWrapWidth (my ps, 6.0 - x);
		Graphics_setSecondIndent (my ps, secondIndent);
		Graphics_setFontStyle (my ps, style);
		Graphics_text (my ps, my d_x, my d_y, text);
		if (method & HyperPage_ADD_BORDER) {
			Graphics_setLineWidth (my ps, 3.0);
			/*Graphics_line (my ps, 0.7, my d_y, 6.7, my d_y);*/
			Graphics_line (my ps, 0.7, my d_y + size * (1.2/72) + 0.07, 6.7, my d_y + size * (1.2/72) + 0.07);
			Graphics_setLineWidth (my ps, 1.0);
		}
		my d_y = Graphics_inqTextY (my ps);
	}
	my previousBottomSpacing = bottomSpacing;
}

void HyperPage_pageTitle (HyperPage me, conststring32 title) {
	HyperPage_any (me, title, my instancePref_font(), my instancePref_fontSize() * 2.0, 0,
		2.0, 0.0, 0.0, my printing ? 0.4/2 : 0.2/2, 0.3/2, HyperPage_ADD_BORDER);
}
void HyperPage_intro (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.03, 0.0, 0.1, 0.1, 0);
}
void HyperPage_entry (HyperPage me, conststring32 title) {
	HyperPage_any (me, title, my instancePref_font(), my instancePref_fontSize() * 1.4, Graphics_BOLD, 0.5, 0.0, 0.0, 0.25/1.4, 0.1/1.4, HyperPage_USE_ENTRY_HINT);
}
void HyperPage_subheader (HyperPage me, conststring32 title) {
	HyperPage_any (me, title, my instancePref_font(), my instancePref_fontSize() * 1.2, Graphics_BOLD, 0.4, 0.0, 0.0, 0.25/1.2, 0.1/1.2, HyperPage_USE_ENTRY_HINT);
}
void HyperPage_paragraph (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.03, 0.0, 0.1, 0.1, 0);
}
void HyperPage_listItem (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.30, 0.2, 0.0, 0.0, 0);
}
void HyperPage_listItem1 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.57, 0.2, 0.0, 0.0, 0);
}
void HyperPage_listItem2 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.84, 0.2, 0.0, 0.0, 0);
}
void HyperPage_listItem3 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 1.11, 0.2, 0.0, 0.0, 0);
}
void HyperPage_listTag (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.2, 0.03, 0.0, 0.1, 0.03, 0);
}
void HyperPage_listTag1 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.2, 0.50, 0.0, 0.05, 0.03, 0);
}
void HyperPage_listTag2 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.2, 0.97, 0.0, 0.03, 0.03, 0);
}
void HyperPage_listTag3 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.2, 1.44, 0.0, 0.03, 0.03, 0);
}
void HyperPage_definition (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.5, 0.0, 0.03, 0.1, 0);
}
void HyperPage_definition1 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.97, 0.0, 0.03, 0.05, 0);
}
void HyperPage_definition2 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 1.44, 0.0, 0.03, 0.03, 0);
}
void HyperPage_definition3 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 1.93, 0.0, 0.03, 0.03, 0);
}
void HyperPage_code0 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 0.0, 0.5, 0.0, 0.0, 0);
}
void HyperPage_code (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 0.3, 0.5, 0.0, 0.0, HyperPage_EXECUTABLE);
}
void HyperPage_code1 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 0.6, 0.5, 0.0, 0.0, 0);
}
void HyperPage_code2 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 0.9, 0.5, 0.0, 0.0, 0);
}
void HyperPage_code3 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 1.2, 0.5, 0.0, 0.0, 0);
}
void HyperPage_code4 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 1.5, 0.5, 0.0, 0.0, 0);
}
void HyperPage_code5 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, kGraphics_font::COURIER, my instancePref_fontSize() * 0.86, 0, 0.0, 1.8, 0.5, 0.0, 0.0, 0);
}
void HyperPage_caption (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize() * 0.86, 0, 0.0, 0.5, 0.5, 0.0, 0.2, 0);
}
void HyperPage_quote (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize() * 0.86, 0, 0.0, 0.5, 0.0, 0.1, 0.1, 0);
}
void HyperPage_quote1 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize() * 0.86, 0, 0.0, 1.0, 0.0, 0.1, 0.1, 0);
}
void HyperPage_quote2 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize() * 0.86, 0, 0.0, 1.5, 0.0, 0.1, 0.1, 0);
}
void HyperPage_quote3 (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize() * 0.86, 0, 0.0, 2.0, 0.0, 0.1, 0.1, 0);
}
void HyperPage_prototype (HyperPage me, conststring32 text) {
	HyperPage_any (me, text, my instancePref_font(), my instancePref_fontSize(), 0, 0.0, 0.03, 0.5, 0.0, 0.0, 0);
}
void HyperPage_formula (HyperPage me, conststring32 formula) {
	constexpr double topSpacing = 0.2, bottomSpacing = 0.2, minFooterDistance = 0.0;
	const kGraphics_font font = my instancePref_font();
	const double size = my instancePref_fontSize();
	if (! my printing) {
		my d_y -= ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0;
		my d_y -= size * (1.2/72);
		if (my d_y > PAGE_HEIGHT + 2.0 || my d_y < PAGE_HEIGHT - SCREEN_HEIGHT) {
		} else {
			Graphics_setFont (my graphics.get(), font);
			Graphics_setFontStyle (my graphics.get(), 0);
			Graphics_setFontSize (my graphics.get(), size);
			Graphics_setWrapWidth (my graphics.get(), 0.0);
			Graphics_setTextAlignment (my graphics.get(), Graphics_CENTRE, Graphics_BOTTOM);
			Graphics_text (my graphics.get(), 0.5 * my rightMargin, my d_y, formula);
			Graphics_setTextAlignment (my graphics.get(), Graphics_LEFT, Graphics_BOTTOM);
		}
	} else {
		Graphics_setFont (my ps, font);
		Graphics_setFontStyle (my ps, 0);
		Graphics_setFontSize (my ps, size);
		my d_y -= ( my d_y == PAPER_TOP - TOP_MARGIN ? 0 : ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0 );
		my d_y -= size * (1.2/72);
		if (my d_y < PAPER_BOTTOM + BOTTOM_MARGIN + minFooterDistance) {
			Graphics_nextSheetOfPaper (my ps);
			if (my d_printingPageNumber) my d_printingPageNumber ++;
			HyperPage_initSheetOfPaper (me);
			Graphics_setFont (my ps, font);
			Graphics_setFontSize (my ps, size);
			my d_y -= size * (1.2/72);
		}
		Graphics_setWrapWidth (my ps, 0.0);
		Graphics_setTextAlignment (my ps, Graphics_CENTRE, Graphics_BOTTOM);
		Graphics_text (my ps, 3.7, my d_y, formula);
		Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_BOTTOM);
	}
	my previousBottomSpacing = bottomSpacing;
}

void HyperPage_picture (HyperPage me, double width_inches, double height_inches, void (*draw) (Graphics g)) {
	constexpr double topSpacing = 0.1, bottomSpacing = 0.1, minFooterDistance = 0.0;
	const kGraphics_font font = my instancePref_font();
	const int size = my instancePref_fontSize();
	width_inches *= ( width_inches < 0.0 ? -1.0 : size / 12.0 );
	height_inches *= ( height_inches < 0.0 ? -1.0 : size / 12.0 );
	if (! my printing) {
		my d_y -= ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0;
		if (my d_y > PAGE_HEIGHT + height_inches || my d_y < PAGE_HEIGHT - SCREEN_HEIGHT) {
			my d_y -= height_inches;
		} else {
			my d_y -= height_inches;
			Graphics_setFont (my graphics.get(), font);
			Graphics_setFontStyle (my graphics.get(), 0);
			Graphics_setFontSize (my graphics.get(), size);
			my d_x = ( width_inches > my rightMargin ? 0.0 : 0.5 * (my rightMargin - width_inches) );
			Graphics_setWrapWidth (my graphics.get(), 0.0);
			Graphics_setViewport (my graphics.get(), my d_x, my d_x + width_inches, my d_y, my d_y + height_inches);
			draw (my graphics.get());
			Graphics_setViewport (my graphics.get(), 0.0, 1.0, 0.0, 1.0);
			Graphics_setWindow (my graphics.get(), 0.0, 1.0, 0.0, 1.0);
			Graphics_setTextAlignment (my graphics.get(), Graphics_LEFT, Graphics_BOTTOM);
		}
	} else {
		Graphics_setFont (my ps, font);
		Graphics_setFontStyle (my ps, 0);
		Graphics_setFontSize (my ps, size);
		my d_y -= ( my d_y == PAPER_TOP - TOP_MARGIN ? 0.0 : ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * size / 12.0 );
		my d_y -= height_inches;
		if (my d_y < PAPER_BOTTOM + BOTTOM_MARGIN + minFooterDistance) {
			Graphics_nextSheetOfPaper (my ps);
			if (my d_printingPageNumber) my d_printingPageNumber ++;
			HyperPage_initSheetOfPaper (me);
			Graphics_setFont (my ps, font);
			Graphics_setFontSize (my ps, size);
			my d_y -= height_inches;
		}
		my d_x = Melder_clippedLeft (0.0, 3.7 - 0.5 * width_inches);
		Graphics_setWrapWidth (my ps, 0.0);
		Graphics_setViewport (my ps, my d_x, my d_x + width_inches, my d_y, my d_y + height_inches);
		draw (my ps);
		Graphics_setViewport (my ps, 0.0, 1.0, 0.0, 1.0);
		Graphics_setWindow (my ps, 0.0, 1.0, 0.0, 1.0);
		Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_BOTTOM);
	}
	my previousBottomSpacing = bottomSpacing;
}

void HyperPage_script (HyperPage me, double width_inches, double height_inches, conststring32 script,
	Graphics cacheGraphics, conststring32 cacheInfo)
{
	if (cacheInfo && cacheInfo [0] != U'\0') {
		const double fontSize = my instancePref_fontSize() * 0.86;   // as in CODE
		Graphics_setColour (my graphics.get(), Melder_MAGENTA);
		HyperPage_code0 (me, U"\\#{=>}");
		Graphics_setColour (my graphics.get(), Melder_BLACK);
		//my d_y -= fontSize * (1.0/72);   // some empty space between the code and the output
		static MelderString buffer;
		MelderString_empty (& buffer);
		for (const char32 *p = & cacheInfo [0]; *p != U'\0'; p ++) {
			if (Melder_isEndOfLine (*p)) {
				if (
					str32str (buffer.string, U"**AN ERROR OCCURRED IN THIS CODE CHUNK:**") ||
					str32str (buffer.string, U"**ERROR** This code chunk was not run,")
				)
					Graphics_setColour (my graphics.get(), Melder_RED);
				if (str32chr (buffer.string, U'\t'))   // is there a tab *anywhere* in the line?
					HyperPage_code (me, Melder_cat (U"\t", buffer.string));   // ... then make sure that the first column is also tabbed
				else
					HyperPage_code (me, buffer.string);
				MelderString_empty (& buffer);
			} else {
				MelderString_appendCharacter (& buffer, *p);
			}
		}
		Graphics_setColour (my graphics.get(), Melder_BLACK);
	}
	autostring32 text = Melder_dup (script);
	constexpr double topSpacing = 0.1, bottomSpacing = 0.1, minFooterDistance = 0.0;
	const kGraphics_font font = my instancePref_font();
	const double fontSize = my instancePref_fontSize();
	const double true_width_inches = width_inches * ( width_inches < 0.0 ? -1.0 : fontSize / 12.0 );
	const double true_height_inches = height_inches * ( height_inches < 0.0 ? -1.0 : fontSize / 12.0 );
	if (! my printing) {
		my d_y -= ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * fontSize / 12.0;
		if (my d_y > PAGE_HEIGHT + true_height_inches || my d_y < PAGE_HEIGHT - SCREEN_HEIGHT) {
			my d_y -= true_height_inches;
		} else {
			my d_y -= true_height_inches;
			Graphics_setFont (my graphics.get(), font);
			Graphics_setFontStyle (my graphics.get(), 0);
			Graphics_setFontSize (my graphics.get(), fontSize);
			my d_x = ( true_width_inches > my rightMargin ? 0.0 : 0.5 * (my rightMargin - true_width_inches) );
			Graphics_setWrapWidth (my graphics.get(), 0.0);
			integer x1DCold, x2DCold, y1DCold, y2DCold;
			Graphics_inqWsViewport (my graphics.get(), & x1DCold, & x2DCold, & y1DCold, & y2DCold);
			double x1NDCold, x2NDCold, y1NDCold, y2NDCold;
			Graphics_inqWsWindow (my graphics.get(), & x1NDCold, & x2NDCold, & y1NDCold, & y2NDCold);
			{
				if (! my praatApplication)
					my praatApplication = Melder_calloc_f (structPraatApplication, 1);
				if (! my praatObjects)
					my praatObjects = Melder_calloc_f (structPraatObjects, 1);
				if (! my praatPicture)
					my praatPicture = Melder_calloc_f (structPraatPicture, 1);
				theCurrentPraatApplication = my praatApplication;
				theCurrentPraatApplication -> batch = true;   // prevent creation of editor windows
				theCurrentPraatApplication -> topShell = theForegroundPraatApplication. topShell;   // needed for UiForm_create () in dialogs
				theCurrentPraatObjects = my praatObjects;
				theCurrentPraatPicture = my praatPicture;
				theCurrentPraatPicture -> graphics = my graphics.get();   // has to draw into HyperPage rather than Picture window
				theCurrentPraatPicture -> font = font;
				theCurrentPraatPicture -> fontSize = fontSize;
				theCurrentPraatPicture -> lineType = Graphics_DRAWN;
				theCurrentPraatPicture -> colour = Melder_BLACK;
				theCurrentPraatPicture -> lineWidth = 1.0;
				theCurrentPraatPicture -> arrowSize = 1.0;
				theCurrentPraatPicture -> speckleSize = 1.0;
				theCurrentPraatPicture -> x1NDC = my d_x;
				theCurrentPraatPicture -> x2NDC = my d_x + true_width_inches;
				theCurrentPraatPicture -> y1NDC = my d_y;
				theCurrentPraatPicture -> y2NDC = my d_y + true_height_inches;

				Graphics_setViewport (my graphics.get(), theCurrentPraatPicture -> x1NDC, theCurrentPraatPicture -> x2NDC, theCurrentPraatPicture -> y1NDC, theCurrentPraatPicture -> y2NDC);
				Graphics_setWindow (my graphics.get(), 0.0, 1.0, 0.0, 1.0);
				integer x1DC, y1DC, x2DC, y2DC;
				Graphics_WCtoDC (my graphics.get(), 0.0, 0.0, & x1DC, & y2DC);
				Graphics_WCtoDC (my graphics.get(), 1.0, 1.0, & x2DC, & y1DC);
				Graphics_resetWsViewport (my graphics.get(), x1DC, x2DC, y1DC, y2DC);
				Graphics_setWsWindow (my graphics.get(), 0, width_inches, 0, height_inches);
				theCurrentPraatPicture -> x1NDC = 0.0;
				theCurrentPraatPicture -> x2NDC = width_inches;
				theCurrentPraatPicture -> y1NDC = 0.0;
				theCurrentPraatPicture -> y2NDC = height_inches;
				Graphics_setViewport (my graphics.get(), theCurrentPraatPicture -> x1NDC, theCurrentPraatPicture -> x2NDC, theCurrentPraatPicture -> y1NDC, theCurrentPraatPicture -> y2NDC);

				{// scope
					autoMelderProgressOff progress;
					autoMelderWarningOff nowarn;
					autoMelderSaveCurrentFolder saveFolder;
					if (! MelderFolder_isNull (& my rootDirectory))
						Melder_setCurrentFolder (& my rootDirectory);
					Melder_assert (cacheGraphics);
					const bool dollarSignWasCode = my graphics -> dollarSignIsCode;
					const bool backquoteWasVerbatim = my graphics -> backquoteIsVerbatim;
					const bool atSignWasLink = my graphics -> atSignIsLink;
					Graphics_setDollarSignIsCode (my graphics.get(), false);
					Graphics_setBackquoteIsVerbatim (my graphics.get(), false);
					Graphics_setAtSignIsLink (my graphics.get(), false);
					Graphics_play (cacheGraphics, my graphics.get());
					if (dollarSignWasCode)
						Graphics_setDollarSignIsCode (my graphics.get(), true);
					if (backquoteWasVerbatim)
						Graphics_setBackquoteIsVerbatim (my graphics.get(), true);
					if (atSignWasLink)
						Graphics_setAtSignIsLink (my graphics.get(), true);
				}
				Graphics_setLineType (my graphics.get(), Graphics_DRAWN);
				Graphics_setLineWidth (my graphics.get(), 1.0);
				Graphics_setArrowSize (my graphics.get(), 1.0);
				Graphics_setSpeckleSize (my graphics.get(), 1.0);
				Graphics_setColour (my graphics.get(), Melder_BLACK);
				theCurrentPraatApplication = & theForegroundPraatApplication;
				theCurrentPraatObjects = & theForegroundPraatObjects;
				theCurrentPraatPicture = & theForegroundPraatPicture;
			}
			Graphics_resetWsViewport (my graphics.get(), x1DCold, x2DCold, y1DCold, y2DCold);
			Graphics_setWsWindow (my graphics.get(), x1NDCold, x2NDCold, y1NDCold, y2NDCold);
			Graphics_setViewport (my graphics.get(), 0.0, 1.0, 0.0, 1.0);
			Graphics_setWindow (my graphics.get(), 0.0, 1.0, 0.0, 1.0);
			Graphics_setTextAlignment (my graphics.get(), Graphics_LEFT, Graphics_BOTTOM);
		}
	} else {
		Graphics_setFont (my ps, font);
		Graphics_setFontStyle (my ps, 0);
		Graphics_setFontSize (my ps, fontSize);
		my d_y -= ( my d_y == PAPER_TOP - TOP_MARGIN ? 0.0 : ( my previousBottomSpacing > topSpacing ? my previousBottomSpacing : topSpacing ) * fontSize / 12.0 );
		my d_y -= true_height_inches;
		if (my d_y < PAPER_BOTTOM + BOTTOM_MARGIN + minFooterDistance) {
			Graphics_nextSheetOfPaper (my ps);
			if (my d_printingPageNumber) my d_printingPageNumber ++;
			HyperPage_initSheetOfPaper (me);
			Graphics_setFont (my ps, font);
			Graphics_setFontSize (my ps, fontSize);
			my d_y -= true_height_inches;
		}
		my d_x = Melder_clippedLeft (0.0, 3.7 - 0.5 * true_width_inches);
		Graphics_setWrapWidth (my ps, 0.0);
		integer x1DCold, x2DCold, y1DCold, y2DCold;
		Graphics_inqWsViewport (my ps, & x1DCold, & x2DCold, & y1DCold, & y2DCold);
		double x1NDCold, x2NDCold, y1NDCold, y2NDCold;
		Graphics_inqWsWindow (my ps, & x1NDCold, & x2NDCold, & y1NDCold, & y2NDCold);
		{
			if (! my praatApplication)
				my praatApplication = Melder_calloc_f (structPraatApplication, 1);
			if (! my praatObjects)
				my praatObjects = Melder_calloc_f (structPraatObjects, 1);
			if (! my praatPicture)
				my praatPicture = Melder_calloc_f (structPraatPicture, 1);
			theCurrentPraatApplication = my praatApplication;
			theCurrentPraatApplication -> batch = true;
			theCurrentPraatApplication -> topShell = theForegroundPraatApplication. topShell;   // needed for UiForm_create () in dialogs
			theCurrentPraatObjects = my praatObjects;
			theCurrentPraatPicture = my praatPicture;
			theCurrentPraatPicture -> graphics = my ps;
			theCurrentPraatPicture -> font = font;
			theCurrentPraatPicture -> fontSize = fontSize;
			theCurrentPraatPicture -> lineType = Graphics_DRAWN;
			theCurrentPraatPicture -> colour = Melder_BLACK;
			theCurrentPraatPicture -> lineWidth = 1.0;
			theCurrentPraatPicture -> arrowSize = 1.0;
			theCurrentPraatPicture -> speckleSize = 1.0;
			theCurrentPraatPicture -> x1NDC = my d_x;
			theCurrentPraatPicture -> x2NDC = my d_x + true_width_inches;
			theCurrentPraatPicture -> y1NDC = my d_y;
			theCurrentPraatPicture -> y2NDC = my d_y + true_height_inches;

			Graphics_setViewport (my ps, theCurrentPraatPicture -> x1NDC, theCurrentPraatPicture -> x2NDC, theCurrentPraatPicture -> y1NDC, theCurrentPraatPicture -> y2NDC);
			Graphics_setWindow (my ps, 0.0, 1.0, 0.0, 1.0);
			integer x1DC, y1DC, x2DC, y2DC;
			Graphics_WCtoDC (my ps, 0.0, 0.0, & x1DC, & y2DC);
			Graphics_WCtoDC (my ps, 1.0, 1.0, & x2DC, & y1DC);
			integer shift = (integer) (Graphics_getResolution (my ps) * true_height_inches) + (y1DCold - y2DCold);
			#if cocoa
				shift = 0;   // this is a FIX
			#endif
			Graphics_resetWsViewport (my ps, x1DC, x2DC, y1DC + shift, y2DC + shift);
			Graphics_setWsWindow (my ps, 0, width_inches, 0, height_inches);
			theCurrentPraatPicture -> x1NDC = 0.0;
			theCurrentPraatPicture -> x2NDC = width_inches;
			theCurrentPraatPicture -> y1NDC = 0.0;
			theCurrentPraatPicture -> y2NDC = height_inches;
			Graphics_setViewport (my ps, theCurrentPraatPicture -> x1NDC, theCurrentPraatPicture -> x2NDC, theCurrentPraatPicture -> y1NDC, theCurrentPraatPicture -> y2NDC);

			{// scope
				autoMelderProgressOff progress;
				autoMelderWarningOff nowarn;
				autoMelderSaveCurrentFolder saveFolder;
				if (! MelderFolder_isNull (& my rootDirectory))
					Melder_setCurrentFolder (& my rootDirectory);
				try {
					//Interpreter_run (interpreter, text.get(), false);   // BUG: implement
				} catch (MelderError) {
					Melder_clearError ();
				}
			}
			Graphics_setLineType (my ps, Graphics_DRAWN);
			Graphics_setLineWidth (my ps, 1.0);
			Graphics_setArrowSize (my ps, 1.0);
			Graphics_setSpeckleSize (my ps, 1.0);
			Graphics_setColour (my ps, Melder_BLACK);
			theCurrentPraatApplication = & theForegroundPraatApplication;
			theCurrentPraatObjects = & theForegroundPraatObjects;
			theCurrentPraatPicture = & theForegroundPraatPicture;
		}
		Graphics_resetWsViewport (my ps, x1DCold, x2DCold, y1DCold, y2DCold);
		Graphics_setWsWindow (my ps, x1NDCold, x2NDCold, y1NDCold, y2NDCold);
		Graphics_setViewport (my ps, 0.0, 1.0, 0.0, 1.0);
		Graphics_setWindow (my ps, 0.0, 1.0, 0.0, 1.0);
		Graphics_setTextAlignment (my ps, Graphics_LEFT, Graphics_BOTTOM);
	}
	my previousBottomSpacing = bottomSpacing;
}

static void print (void *void_me, Graphics graphics) {
	iam (HyperPage);
	my ps = graphics;
	Graphics_setDollarSignIsCode (graphics, true);   // for manuals
	Graphics_setBackquoteIsVerbatim (graphics, true);   // for notebooks
	Graphics_setAtSignIsLink (graphics, true);
	my printing = true;
	HyperPage_initSheetOfPaper (me);
	my v_draw ();
	my printing = false;
}

/********** class HyperPage **********/

void structHyperPage :: v9_destroy () noexcept {
	if (our praatApplication) {
		for (integer iobject = ((PraatObjects) our praatObjects) -> n; iobject >= 1; iobject --) {
			((PraatObjects) our praatObjects) -> list [iobject]. name. reset();
			forget (((PraatObjects) our praatObjects) -> list [iobject]. object);
		}
		Melder_free (our praatApplication);
		Melder_free (our praatObjects);
		Melder_free (our praatPicture);
	}
	our HyperPage_Parent :: v9_destroy ();
}

static void gui_drawingarea_cb_expose (HyperPage me, GuiDrawingArea_ExposeEvent /* event */) {
	trace (U"HyperPage: gui_drawingarea_cb_expose");
	if (! my graphics)
		return;   // could be the case in the very beginning
	if (my entryHint && my entryPosition != 0.0) {
		my entryHint. reset();
		my top = 5.0 * (PAGE_HEIGHT - my entryPosition);
		Melder_clipLeft (0.0, & my top);
	}
	my d_y = PAGE_HEIGHT + my top / 5.0;
	my d_x = 0.0;
	my previousBottomSpacing = 0.0;
	my links. removeAllItems ();
	trace (U"going to draw");
	Graphics_clearWs (my graphics.get());
	my v_draw ();
}

static void gui_drawingarea_cb_mouse (HyperPage me, GuiDrawingArea_MouseEvent event) {
	if (! event -> isClick())
		return;
	if (! my graphics)
		return;   // could be the case in the very beginning
	for (integer ilink = 1; ilink <= my links.size; ilink ++) {
		HyperLink link = my links.at [ilink];
		if (! link)
			Melder_fatal (U"gui_drawingarea_cb_click: empty link ", ilink, U"/", my links.size, U".");
		if (event -> y > link -> y2DC && event -> y < link -> y1DC && event -> x > link -> x1DC && event -> x < link -> x2DC) {
			saveHistory (me, my optionalCurrentPageTitle.get());
			try {
				HyperPage_goToPage (me, link -> name.get());
			} catch (MelderError) {
				Melder_flushError ();
			}
			return;
		}
	}
}

extern "C" void GRAPHICS_PostScript_settings (UiForm sendingForm, int narg, Stackel args, conststring32 sendingString, Interpreter interpreter, conststring32 invokingButtonTitle, bool modified, void *buttonClosure);

static void menu_cb_postScriptSettings (HyperPage me, EDITOR_ARGS) {
	(void) me;
	(void) cmd;
	GRAPHICS_PostScript_settings (_sendingForm_, _narg_, _args_, _sendingString_, optionalInterpreter, nullptr, false, nullptr);
}

#ifdef macintosh
static void menu_cb_pageSetup (HyperPage me, EDITOR_ARGS) {
	(void) me;
	Printer_pageSetup ();
}
#endif

static void menu_cb_print (HyperPage me, EDITOR_ARGS) {
	EDITOR_FORM (U"Print", nullptr)
		SENTENCE_FIELD (my insideHeader, U"Left or inside header", U"")
		SENTENCE_FIELD (my middleHeader, U"Middle header", U"")
		TEXTFIELD_FIELD (my outsideHeader, U"Right or outside header", U"", 2)
		SENTENCE_FIELD (my insideFooter, U"Left or inside footer", U"")
		SENTENCE_FIELD (my middleFooter, U"Middle footer", U"")
		SENTENCE_FIELD (my outsideFooter, U"Right or outside footer", U"")
		BOOLEAN_FIELD (my mirror, U"Mirror even/odd headers", true)
		INTEGER_FIELD (my d_printingPageNumber, U"First page number", U"0 (= no page numbers)")
	EDITOR_OK
		my v_defaultHeaders (cmd);
		if (my d_printingPageNumber != 0)
			SET_INTEGER (my d_printingPageNumber, my d_printingPageNumber + 1)
	EDITOR_DO
		Printer_print (print, me);
	EDITOR_END
}

static void menu_cb_font (HyperPage me, EDITOR_ARGS) {
	EDITOR_FORM (U"Font", nullptr)
		CHOICE (font, U"Font", 1)
			OPTION (U"Times")
			OPTION (U"Helvetica")
	EDITOR_OK
		SET_OPTION (font, my instancePref_font() == kGraphics_font::TIMES ? 1 :
				my instancePref_font() == kGraphics_font::HELVETICA ? 2 : my instancePref_font() == kGraphics_font::PALATINO ? 3 : 1);
	EDITOR_DO
		my setInstancePref_font ( font == 1 ? kGraphics_font::TIMES : kGraphics_font::HELVETICA );
		Editor_dataChanged (me, me);
		if (my graphics)
			Graphics_updateWs (my graphics.get());
	EDITOR_END
}

static void updateSizeMenu (HyperPage me) {
	GuiMenuItem_check (my fontSizeButton_10, my instancePref_fontSize() == 10);
	GuiMenuItem_check (my fontSizeButton_12, my instancePref_fontSize() == 12);
	GuiMenuItem_check (my fontSizeButton_14, my instancePref_fontSize() == 14);
	GuiMenuItem_check (my fontSizeButton_18, my instancePref_fontSize() == 18);
	GuiMenuItem_check (my fontSizeButton_24, my instancePref_fontSize() == 24);
}
static void setFontSize (HyperPage me, double fontSize) {
	my setInstancePref_fontSize (fontSize);
	Editor_dataChanged (me, me);
	updateSizeMenu (me);
	if (my graphics)
		Graphics_updateWs (my graphics.get());
}

static void menu_cb_10 (HyperPage me, EDITOR_ARGS) { setFontSize (me, 10.0); }
static void menu_cb_12 (HyperPage me, EDITOR_ARGS) { setFontSize (me, 12.0); }
static void menu_cb_14 (HyperPage me, EDITOR_ARGS) { setFontSize (me, 14.0); }
static void menu_cb_18 (HyperPage me, EDITOR_ARGS) { setFontSize (me, 18.0); }
static void menu_cb_24 (HyperPage me, EDITOR_ARGS) { setFontSize (me, 24.0); }

static void menu_cb_fontSize (HyperPage me, EDITOR_ARGS) {
	EDITOR_FORM (U"Font size", nullptr)
		POSITIVE (fontSize, U"Font size (points)", my default_fontSize ())
	EDITOR_OK
		SET_REAL (fontSize, my instancePref_fontSize())
	EDITOR_DO
		setFontSize (me, fontSize);
	EDITOR_END
}

static void menu_cb_searchForPage (HyperPage me, EDITOR_ARGS) {
	EDITOR_FORM (U"Search for page", nullptr)
		TEXTFIELD (page, U"Page", U"a", 2)
	EDITOR_OK
	EDITOR_DO
		HyperPage_goToPage (me, page);
	EDITOR_END
}

/********************************************************************************
 *
 * The vertical scroll bar controls and/or mirrors
 * the position of the viewable area within the page.
 * A page can be PAGE_HEIGHT inches high, so 'my top' (and the scroll-bar 'value')
 * may take on values between 0 and PAGE_HEIGHT * 5 (fifth inches).
 * Hence, the 'minimum' is 0.
 * The viewable area shows a certain number of fifth inches;
 * hence the 'sliderSize' is height / resolution * 5,
 * and the 'maximum' is PAGE_HEIGHT * 5.
 * The 'increment' is 1, so the arrows move the page by one fifth of an inch.
 * The 'pageIncrement' is sliderSize - 1.
 */

static void gui_cb_verticalScroll (HyperPage me, GuiScrollBarEvent	event) {
	trace (U"gui_cb_verticalScroll");
	const double value = GuiScrollBar_getValue (event -> scrollBar);
	if (value != my top) {
		trace (U"scroll from ", my top, U" to ", value);
		my top = value;
		updateVerticalScrollBar (me);
		Graphics_updateWs (my graphics.get());
	}
}

static void createVerticalScrollBar (HyperPage me, GuiForm parent) {
	const int height = Machine_getTextHeight ();
	my verticalScrollBar = GuiScrollBar_createShown (parent,
		- Machine_getScrollBarWidth (), 0,
		Machine_getMenuBarBottom () + (my d_hasExtraRowOfTools ? 2 * height + 19 : height + 12), - Machine_getScrollBarWidth (),
		0, PAGE_HEIGHT * 5, 0, 25, 1, 24,
		gui_cb_verticalScroll, me, 0
	);
}

static void updateVerticalScrollBar (HyperPage me)
/* We cannot call this immediately after creation. */
/* This has to be called after changing `my top`. */
{
	trace (U"updateVerticalScrollBar");
	const int sliderSize = 25;
	GuiScrollBar_set (my verticalScrollBar, undefined, undefined, my top, sliderSize, 1, sliderSize - 1);
	my history [my historyPointer]. top = 0/*my top*/;
}

static void menu_cb_pageUp (HyperPage me, EDITOR_ARGS) {
	if (! my verticalScrollBar)
		return;
	const double value = Melder_clippedLeft (0.0, GuiScrollBar_getValue (my verticalScrollBar) - 24.0);
	if (value != my top) {
		my top = value;
		updateVerticalScrollBar (me);
		Graphics_updateWs (my graphics.get());
	}
}

static void menu_cb_pageDown (HyperPage me, EDITOR_ARGS) {
	if (! my verticalScrollBar)
		return;
	const double value = Melder_clippedRight (GuiScrollBar_getValue (my verticalScrollBar) + 24.0, (PAGE_HEIGHT * 5.0) - 25.0);
	if (value != my top) {
		my top = value;
		updateVerticalScrollBar (me);
		Graphics_updateWs (my graphics.get());
	}
}

/********** **********/

static void do_back (HyperPage me) {
	if (my historyPointer <= 0)
		return;
	autostring32 pageTitle = Melder_dup_f (my history [-- my historyPointer]. page.get());   // temporary, because pointer will be moved
	Melder_assert (pageTitle);
	const double top = my history [my historyPointer]. top;
	if (my v_goToPage (pageTitle.get())) {
		my top = top;
		updateVerticalScrollBar (me);
		HyperPage_clear (me);
	}
}

static void menu_cb_back (HyperPage me, EDITOR_ARGS) {
	do_back (me);
}

static void gui_button_cb_back (HyperPage me, GuiButtonEvent /* event */) {
	do_back (me);
}

static void do_forth (HyperPage me) {
	if (my historyPointer >= 19 || ! my history [my historyPointer + 1]. page)
		return;
	autostring32 pageTitle = Melder_dup_f (my history [++ my historyPointer]. page.get());
	Melder_assert (pageTitle);
	const double top = my history [my historyPointer]. top;
	if (my v_goToPage (pageTitle.get())) {
		my top = top;
		updateVerticalScrollBar (me);
		HyperPage_clear (me);
	}
}

static void menu_cb_forth (HyperPage me, EDITOR_ARGS) {
	do_forth (me);
}

static void gui_button_cb_forth (HyperPage me, GuiButtonEvent /* event */) {
	do_forth (me);
}

void structHyperPage :: v_createMenus () {
	HyperPage_Parent :: v_createMenus ();

	Editor_addCommand (this, U"File", U"PostScript settings...", GuiMenu_HIDDEN, menu_cb_postScriptSettings);
	#ifdef macintosh
		Editor_addCommand (this, U"File", U"Page setup...", GuiMenu_HIDDEN, menu_cb_pageSetup);
	#endif
	Editor_addCommand (this, U"File", U"Print page...", GuiMenu_HIDDEN, menu_cb_print);
	Editor_addCommand (this, U"File", U"-- close --", GuiMenu_HIDDEN, nullptr);

	if (our v_hasHistory ()) {
		Editor_addMenu (this, U"Go to", 0);
		Editor_addCommand (this, U"Go to", U"Back", GuiMenu_OPTION | GuiMenu_LEFT_ARROW, menu_cb_back);
		Editor_addCommand (this, U"Go to", U"Forward", GuiMenu_OPTION | GuiMenu_RIGHT_ARROW, menu_cb_forth);
		Editor_addCommand (this, U"Go to", U"-- page --", 0, nullptr);
		Editor_addCommand (this, U"Go to", U"Page up", GuiMenu_PAGE_UP, menu_cb_pageUp);
		Editor_addCommand (this, U"Go to", U"Page down", GuiMenu_PAGE_DOWN, menu_cb_pageDown);
		Editor_addCommand (this, U"Go to", U"-- page --", 0, nullptr);
		Editor_addCommand (this, U"Go to", U"Search for page...", 0, menu_cb_searchForPage);
	}

	Editor_addMenu (this, U"Font", 0);
	Editor_addCommand (this, U"Font", U"Font size...", 0, menu_cb_fontSize);
	fontSizeButton_10 = Editor_addCommand (this, U"Font", U"10", GuiMenu_CHECKBUTTON, menu_cb_10);
	fontSizeButton_12 = Editor_addCommand (this, U"Font", U"12", GuiMenu_CHECKBUTTON, menu_cb_12);
	fontSizeButton_14 = Editor_addCommand (this, U"Font", U"14", GuiMenu_CHECKBUTTON, menu_cb_14);
	fontSizeButton_18 = Editor_addCommand (this, U"Font", U"18", GuiMenu_CHECKBUTTON, menu_cb_18);
	fontSizeButton_24 = Editor_addCommand (this, U"Font", U"24", GuiMenu_CHECKBUTTON, menu_cb_24);
	Editor_addCommand (this, U"Font", U"-- font --", 0, nullptr);
	Editor_addCommand (this, U"Font", U"Font...", 0, menu_cb_font);
}

/********** **********/

static void gui_drawingarea_cb_resize (HyperPage me, GuiDrawingArea_ResizeEvent event) {
	trace (U"HyperPage: gui_drawingarea_cb_resize");
	if (! my graphics)
		return;
	Graphics_setWsViewport (my graphics.get(), 0.0, event -> width, 0.0, event -> height);
	Graphics_setWsWindow (my graphics.get(), 0.0, my rightMargin = event -> width / resolution,
			PAGE_HEIGHT - event -> height / resolution, PAGE_HEIGHT);
	//updateVerticalScrollBar (me);
	//Graphics_updateWs (my graphics.get());
}

static void gui_button_cb_previousPage (HyperPage me, GuiButtonEvent /* event */) {
	HyperPage_goToPage_number (me, my v_getCurrentPageNumber () > 1 ?
			my v_getCurrentPageNumber () - 1 : my v_getNumberOfPages ());
}

static void gui_button_cb_nextPage (HyperPage me, GuiButtonEvent /* event */) {
	const integer currentPageNumber = my v_getCurrentPageNumber ();
	HyperPage_goToPage_number (me, currentPageNumber < my v_getNumberOfPages () ? currentPageNumber + 1 : 1);
}

void structHyperPage :: v_createChildren () {
	const int height = Machine_getTextHeight ();
	const int y = Machine_getMenuBarBottom () + 4;

	/***** Create navigation buttons. *****/

	if (our v_hasHistory ()) {
		GuiButton_createShown (our windowForm, 4, 48, y, y + height,
				U"<", gui_button_cb_back, this, 0);
		GuiButton_createShown (our windowForm, 54, 98, y, y + height,
				U">", gui_button_cb_forth, this, 0);
	}
	if (our v_isOrdered ()) {
		GuiButton_createShown (our windowForm, 174, 218, y, y + height,
				U"< 1", gui_button_cb_previousPage, this, 0);
		GuiButton_createShown (our windowForm, 224, 268, y, y + height,
				U"1 >", gui_button_cb_nextPage, this, 0);
	}

	/***** Create scroll bar. *****/

	createVerticalScrollBar (this, our windowForm);

	/***** Create drawing area. *****/

	drawingArea = GuiDrawingArea_createShown (our windowForm,
		0, - Machine_getScrollBarWidth (),
		y + ( our d_hasExtraRowOfTools ? 2 * height + 16 : height + 9 ), - Machine_getScrollBarWidth (),
		gui_drawingarea_cb_expose, gui_drawingarea_cb_mouse,
		nullptr, gui_drawingarea_cb_resize, nullptr, this, GuiDrawingArea_BORDER
	);
	GuiDrawingArea_setSwipable (drawingArea, nullptr, our verticalScrollBar);
}

void HyperPage_init1 (HyperPage me, conststring32 title, Daata data, bool backquoteIsVerbatim) {
	resolution = Gui_getResolution (nullptr);
	const int width = 760;   // used to be (int) floor (6 * resolution + 30)
	Editor_init (me, 0, 0, width, 800, title, data);
	#if motif
		Melder_assert (XtWindow (my drawingArea -> d_widget));
	#endif
	my graphics = Graphics_create_xmdrawingarea (my drawingArea);
	Graphics_setDollarSignIsCode (my graphics.get(), true);   // for manuals
	if (backquoteIsVerbatim)
		Graphics_setBackquoteIsVerbatim (my graphics.get(), true);   // for notebooks
	Graphics_setAtSignIsLink (my graphics.get(), true);
	Graphics_setFont (my graphics.get(), kGraphics_font::TIMES);
	if (my instancePref_font() != kGraphics_font::TIMES && my instancePref_font() != kGraphics_font::HELVETICA)
		my setInstancePref_font (kGraphics_font::TIMES);   // ensure Unicode compatibility
}
void HyperPage_init2 (HyperPage me, conststring32 /* title */, Daata /* data */) {
	/*
		Relevant parts from the dataChanged message:
	*/
	updateSizeMenu (me);
	Graphics_updateWs (my graphics.get());

struct structGuiDrawingArea_ResizeEvent event { my drawingArea, 0, 0 };
event. width  = GuiControl_getWidth  (my drawingArea);
event. height = GuiControl_getHeight (my drawingArea);
gui_drawingarea_cb_resize (me, & event);

	updateVerticalScrollBar (me);   // scroll to the top (my top == 0)
}
void HyperPage_init (HyperPage me, conststring32 title, Daata data) {
	HyperPage_init1 (me, title, data, false);
	HyperPage_init2 (me, title, data);
}

void HyperPage_clear (HyperPage me) {
	my links. removeAllItems();
	Graphics_updateWs (my graphics.get());
}

void structHyperPage :: v1_dataChanged (Editor /* sender */) {
	const bool oldError = Melder_hasError ();   // this method can be called during error time
	(void) our v_goToPage (our optionalCurrentPageTitle.get());
	if (Melder_hasError () && ! oldError)
		Melder_flushError ();
	updateVerticalScrollBar (this);
	HyperPage_clear (this);
}

int HyperPage_goToPage (HyperPage me, conststring32 pageTitle) {
	Melder_assert (pageTitle);
	switch (my v_goToPage (pageTitle)) {
		case -1: return 0;
		case 0: HyperPage_clear (me); return 0;
	}
	saveHistory (me, pageTitle);   // last chance: HyperPage_clear will destroy "title" !!!
	my optionalCurrentPageTitle = Melder_dup_f (pageTitle);
	Melder_assert (my optionalCurrentPageTitle);
	my top = 0.0;
	updateVerticalScrollBar (me);   // scroll to the top (my top == 0)
	HyperPage_clear (me);
	return 1;
}

void HyperPage_goToPage_number (HyperPage me, integer optionalPageNumber) {
	my v_goToPage_number (optionalPageNumber);   // catch -> HyperPage_clear (me); ?
	Melder_assert (! optionalPageNumber || my optionalCurrentPageTitle);   // only page 0 has no title
	my top = 0.0;
	updateVerticalScrollBar (me);   // scroll to the top (my top == 0.0)
	HyperPage_clear (me);
}

void HyperPage_setEntryHint (HyperPage me, conststring32 hint) {
	my entryHint = Melder_dup_f (hint);
}

/* End of file HyperPage.cpp */
