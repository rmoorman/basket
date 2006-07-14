/***************************************************************************
 *   Copyright (C) 2003 by S�astien Laot                                 *
 *   slaout@linux62.org                                                    *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

 /// NEW:

#include <qwidgetstack.h>
#include <qregexp.h>
#include <qpixmap.h>
#include <qpainter.h>
#include <qimage.h>
#include <qbitmap.h>
#include <qwhatsthis.h>
#include <qpopupmenu.h>
#include <qsignalmapper.h>
#include <qdir.h>
#include <kicontheme.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kstringhandler.h>
#include <kmessagebox.h>
#include <kprogress.h>
#include <kstandarddirs.h>
#include <kaboutdata.h>
#include <kwin.h>
#include <kaccel.h>
#include <kpassivepopup.h>
#include <kxmlguifactory.h>
#include <kcmdlineargs.h>
#include <kglobalaccel.h>
#include <kapplication.h>
#include <dcopclient.h>
#include <kdebug.h>
#include <iostream>
#include "bnpview.h"
#include "basket.h"
#include "tools.h"
#include "settings.h"
#include "debugwindow.h"
#include "xmlwork.h"
#include "basketfactory.h"
#include "softwareimporters.h"
#include "colorpicker.h"
#include "regiongrabber.h"
#include "basketlistview.h"
#include "basketproperties.h"
#include "password.h"
#include "newbasketdialog.h"
#include "notedrag.h"
#include "formatimporter.h"
#include "basketstatusbar.h"
#include "backgroundmanager.h"
#include "noteedit.h" // To launch InlineEditors::initToolBars()

/** class BNPView: */

const int BNPView::c_delayTooltipTime = 275;

BNPView::BNPView(QWidget *parent, const char *name, KXMLGUIClient *aGUIClient,
				 KActionCollection *actionCollection, BasketStatusBar *bar)
	: DCOPObject("BasketIface"), QSplitter(Qt::Horizontal, parent, name), m_loading(true), m_newBasketPopup(false),
	m_regionGrabber(0), m_passivePopup(0L), m_actionCollection(actionCollection),
	m_guiClient(aGUIClient), m_statusbar(bar)
{
	/* Settings */
	NoteDrag::createAndEmptyCuttingTmpFolder(); // If last exec hasn't done it: clean the temporary folder we will use
	Settings::loadConfig();

	Global::bnpView = this;

	// Needed when loading the baskets:
	Global::globalAccel       = new KGlobalAccel(this); // FIXME: might be null (KPart case)!
	Global::backgroundManager = new BackgroundManager();

	setupGlobalShortcuts();
	initialize();
	m_statusbar->setupStatusBar();
	QTimer::singleShot(0, this, SLOT(hideRichTextToolBar()));
}

BNPView::~BNPView()
{
	if (currentBasket() && currentBasket()->isDuringEdit())
		currentBasket()->closeEditor();
	Settings::saveConfig();

	Global::bnpView = 0;

	delete Global::tray;
	Global::tray = 0;
	delete m_colorPicker;
	delete m_statusbar;

	NoteDrag::createAndEmptyCuttingTmpFolder(); // Clean the temporary folder we used
}

void BNPView::hideRichTextToolBar()
{
	InlineEditors* instance = InlineEditors::instance();

	if(instance)
	{
		KToolBar* toolbar = instance->richTextToolBar();

		if(toolbar)
			toolbar->hide();
	}
}

void BNPView::setupGlobalShortcuts()
{
	/* Global shortcuts */
	KGlobalAccel *globalAccel = Global::globalAccel; // Better for the following lines

	globalAccel->insert( "global_paste", i18n("Paste clipboard contents in current basket"),
						 i18n("Allows you to paste clipboard contents in the current basket without having to open main window."),
						 Qt::CTRL+Qt::ALT+Qt::Key_V, Qt::CTRL+Qt::ALT+Qt::Key_V,
						 Global::bnpView, SLOT(globalPasteInCurrentBasket()), true, true );
	globalAccel->insert( "global_show_current_basket", i18n("Show current basket name"),
						 i18n("Allows you to know basket is current without opening the main window."),
						 "", "",
						 Global::bnpView, SLOT(showPassiveContentForced()), true, true );
	globalAccel->insert( "global_paste_selection", i18n("Paste selection in current basket"),
						 i18n("Allows you to paste clipboard selection in the current basket without having to open main window."),
						 Qt::CTRL+Qt::ALT+Qt::Key_S, Qt::CTRL+Qt::ALT+Qt::Key_S,
						 Global::bnpView, SLOT(pasteSelInCurrentBasket()),  true, true );
	globalAccel->insert( "global_new_basket", i18n("Create a new basket"),
						 i18n("Allows you to create a new basket without having to open main window (you then can use the other global shortcuts to add a note, paste clipboard or paste selection in this new basket)."),
						 "", "",
						 Global::bnpView, SLOT(askNewBasket()),       true, true );
	globalAccel->insert( "global_previous_basket", i18n("Go to previous basket"),
						 i18n("Allows you to change current basket to the previous one without having to open main window."),
						 "", "",
						 Global::bnpView,    SLOT(goToPreviousBasket()), true, true );
	globalAccel->insert( "global_next_basket", i18n("Go to next basket"),
						 i18n("Allows you to change current basket to the next one without having to open main window."),
						 "", "",
						 Global::bnpView,    SLOT(goToNextBasket()),     true, true );
	globalAccel->insert( "global_note_add_text", i18n("Insert text note"),
						 i18n("Add a text note to the current basket without having to open main window."),
						 "", "", //Qt::CTRL+Qt::ALT+Qt::Key_T, Qt::CTRL+Qt::ALT+Qt::Key_T,
						 Global::bnpView, SLOT(addNoteText()),        true, true );
	globalAccel->insert( "global_note_add_html", i18n("Insert rich text note"),
						 i18n("Add a rich text note to the current basket without having to open main window."),
						 Qt::CTRL+Qt::ALT+Qt::Key_H, Qt::CTRL+Qt::ALT+Qt::Key_H, //"", "",
						 Global::bnpView, SLOT(addNoteHtml()),        true, true );
	globalAccel->insert( "global_note_add_image", i18n("Insert image note"),
						 i18n("Add an image note to the current basket without having to open main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteImage()),       true, true );
	globalAccel->insert( "global_note_add_link", i18n("Insert link note"),
						 i18n("Add a link note to the current basket without having to open main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteLink()),        true, true );
	globalAccel->insert( "global_note_add_color", i18n("Insert color note"),
						 i18n("Add a color note to the current basket without having to open main window."),
						 "", "",
						 Global::bnpView, SLOT(addNoteColor()),       true, true );
	globalAccel->insert( "global_note_pick_color", i18n("Pick color from screen"),
						 i18n("Add a color note picked from one pixel on screen to the current basket without "
								 "having to open main window."),
						 "", "",
						 Global::bnpView, SLOT(slotColorFromScreenGlobal()), true, true );
	globalAccel->insert( "global_note_grab_screenshot", i18n("Grab screen zone"),
						 i18n("Grab a screen zone as an image in the current basket without "
								 "having to open main window."),
						 "", "",
						 Global::bnpView, SLOT(grabScreenshotGlobal()), true, true );
	globalAccel->readSettings();
	globalAccel->updateConnections();
}

void BNPView::initialize()
{
	/// Configure the List View Columns:
	m_tree  = new BasketTreeListView(this);
	m_tree->addColumn(i18n("Baskets"));
	m_tree->setColumnWidthMode(0, QListView::Maximum);
	m_tree->setFullWidth(true);
	m_tree->setSorting(-1/*Disabled*/);
	m_tree->setRootIsDecorated(true);
	m_tree->setTreeStepSize(16);
	m_tree->setLineWidth(1);
	m_tree->setMidLineWidth(0);
	m_tree->setFocusPolicy(QWidget::NoFocus);

	/// Configure the List View Drag and Drop:
	m_tree->setDragEnabled(true);
	m_tree->setAcceptDrops(true);
	m_tree->setItemsMovable(true);
	m_tree->setDragAutoScroll(true);
	m_tree->setDropVisualizer(true);
	m_tree->setDropHighlighter(true);

	/// Configure the Splitter:
	m_stack = new QWidgetStack(this);

	setOpaqueResize(true);

	setCollapsible(m_tree,  true);
	setCollapsible(m_stack, false);
	setResizeMode(m_tree,  QSplitter::KeepSize);
	setResizeMode(m_stack, QSplitter::Stretch);

	int treeWidth = Settings::basketTreeWidth();
	if (treeWidth < 0)
		treeWidth = m_tree->fontMetrics().maxWidth() * 11;
	QValueList<int> sizes;
	sizes.append(treeWidth);
	setSizes(sizes);

	/// Configure the List View Signals:
	connect( m_tree, SIGNAL(returnPressed(QListViewItem*)),    this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(selectionChanged(QListViewItem*)), this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(pressed(QListViewItem*)),          this, SLOT(slotPressed(QListViewItem*)) );
	connect( m_tree, SIGNAL(expanded(QListViewItem*)),         this, SLOT(needSave(QListViewItem*))    );
	connect( m_tree, SIGNAL(collapsed(QListViewItem*)),        this, SLOT(needSave(QListViewItem*))    );
	connect( m_tree, SIGNAL(contextMenu(KListView*, QListViewItem*, const QPoint&)),      this, SLOT(slotContextMenu(KListView*, QListViewItem*, const QPoint&))      );
	connect( m_tree, SIGNAL(mouseButtonPressed(int, QListViewItem*, const QPoint&, int)), this, SLOT(slotMouseButtonPressed(int, QListViewItem*, const QPoint&, int)) );
	connect( m_tree, SIGNAL(doubleClicked(QListViewItem*, const QPoint&, int)), this, SLOT(slotShowProperties(QListViewItem*, const QPoint&, int)) );

	connect( m_tree, SIGNAL(expanded(QListViewItem*)),  this, SIGNAL(basketChanged()) );
	connect( m_tree, SIGNAL(collapsed(QListViewItem*)), this, SIGNAL(basketChanged()) );
	connect( this,   SIGNAL(basketNumberChanged(int)),  this, SIGNAL(basketChanged()) );

	connect( this, SIGNAL(basketNumberChanged(int)), this, SLOT(slotBasketNumberChanged(int)) );
	connect( this, SIGNAL(basketChanged()),          this, SLOT(slotBasketChanged())          );

	setupActions();

	/// What's This Help for the tree:
	QWhatsThis::add(m_tree, i18n(
			"<h2>Basket Tree</h2>"
					"Here is the list of your baskets. "
					"You can organize your data by putting them in different baskets. "
					"You can group baskets by subject by creating new baskets inside others. "
					"You can browse between them by clicking a basket to open it, or reorganize them using drag and drop."));

	setTreePlacement(Settings::treeOnLeft());

	/* System tray icon */
	Global::tray = new ContainerSystemTray(this);
	if (Settings::useSystray())
		Global::tray->show();

	if(!isPart())
	{
		if (Settings::useSystray() && KCmdLineArgs::parsedArgs() && KCmdLineArgs::parsedArgs()->isSet("start-hidden"))
			if(Global::mainWindow()) Global::mainWindow()->hide();
		else if (Settings::useSystray() && kapp->isRestored())
			if(Global::mainWindow()) Global::mainWindow()->setShown(!Settings::startDocked());
		else
			if(Global::mainWindow()) Global::mainWindow()->show();
	}

	// If the main window is hidden when session is saved, Container::queryClose()
	//  isn't called and the last value would be kept
	Settings::setStartDocked(true);

	Tag::loadTags(); // Tags should be ready before loading baskets, but tags need the mainContainer to be ready to create KActions!
	load();

	// If no basket has been found, try to import from an older version,
	if (!firstListViewItem()) {
		QDir dir;
		dir.mkdir(Global::basketsFolder());
		if (FormatImporter::shouldImportBaskets()) {
			FormatImporter::importBaskets();
			load();
		}
		if (!firstListViewItem()) {
		// Create first basket:
			BasketFactory::newBasket(/*icon=*/"", /*name=*/i18n("General"), /*backgroundImage=*/"", /*backgroundColor=*/QColor(), /*textColor=*/QColor(), /*templateName=*/"1column", /*createIn=*/0);
		}
	// TODO: Create Welcome Baskets:
	}
}

void BNPView::setupActions()
{
	m_actHideWindow = new KAction( i18n("&Hide Window"), "", KStdAccel::shortcut(KStdAccel::Close),
								   this, SLOT(hideOnEscape()), actionCollection(), "window_hide" );
	m_actHideWindow->setEnabled(Settings::useSystray()); // Init here !

	new KAction( i18n("&Export to HTML..."), "fileexport", 0,
	             this, SLOT(exportToHTML()),      actionCollection(), "basket_export_html" );
	new KAction( i18n("K&Notes"), "knotes", 0,
	             this, SLOT(importKNotes()),      actionCollection(), "basket_import_knotes" );
	new KAction( i18n("K&Jots"), "kjots", 0,
	             this, SLOT(importKJots()),       actionCollection(), "basket_import_kjots" );
	new KAction( i18n("&KnowIt..."), "knowit", 0,
	             this, SLOT(importKnowIt()),      actionCollection(), "basket_import_knowit" );
	new KAction( i18n("Tux&Cards..."), "tuxcards", 0,
	             this, SLOT(importTuxCards()),    actionCollection(), "basket_import_tuxcards" );
	new KAction( i18n("&Sticky Notes"), "gnome", 0,
	             this, SLOT(importStickyNotes()), actionCollection(), "basket_import_sticky_notes" );
	new KAction( i18n("&Tomboy"), "tintin", 0,
	             this, SLOT(importTomboy()),      actionCollection(), "basket_import_tomboy" );

	/** Note : ****************************************************************/

	m_actDelNote  = new KAction( i18n("D&elete"), "editdelete", "Delete",
								 this, SLOT(delNote()), actionCollection(), "edit_delete" );
	m_actCutNote  = KStdAction::cut(   this, SLOT(cutNote()),               actionCollection() );
	m_actCopyNote = KStdAction::copy(  this, SLOT(copyNote()),              actionCollection() );

	m_actSelectAll = KStdAction::selectAll( this, SLOT( slotSelectAll() ), actionCollection() );
	m_actSelectAll->setStatusText( i18n( "Selects all notes" ) );
	m_actUnselectAll = new KAction( i18n( "U&nselect All" ), "", this, SLOT( slotUnselectAll() ),
									actionCollection(), "edit_unselect_all" );
	m_actUnselectAll->setStatusText( i18n( "Unselects all selected notes" ) );
	m_actInvertSelection = new KAction( i18n( "&Invert Selection" ), CTRL+Key_Asterisk,
										this, SLOT( slotInvertSelection() ),
										actionCollection(), "edit_invert_selection" );
	m_actInvertSelection->setStatusText( i18n( "Inverts the current selection of notes" ) );

	m_actEditNote         = new KAction( i18n("Verb; not Menu", "&Edit..."), "edit",   "Return",
										 this, SLOT(editNote()), actionCollection(), "note_edit" );

	m_actOpenNote         = KStdAction::open( this, SLOT(openNote()), actionCollection(), "note_open" );
	m_actOpenNote->setIcon("window_new");
	m_actOpenNote->setText(i18n("&Open"));
	m_actOpenNote->setShortcut("F9");

	m_actOpenNoteWith     = new KAction( i18n("Open &With..."), "", "Shift+F9",
										 this, SLOT(openNoteWith()), actionCollection(), "note_open_with" );
	m_actSaveNoteAs       = KStdAction::saveAs( this, SLOT(saveNoteAs()), actionCollection(), "note_save_to_file" );
	m_actSaveNoteAs->setIcon("");
	m_actSaveNoteAs->setText(i18n("&Save to File..."));
	m_actSaveNoteAs->setShortcut("F10");

	m_actGroup        = new KAction( i18n("&Group"),          "attach",     "Ctrl+G",
									 this, SLOT(noteGroup()),    actionCollection(), "note_group" );
	m_actUngroup      = new KAction( i18n("U&ngroup"),        "",           "Ctrl+Shift+G",
									 this, SLOT(noteUngroup()),  actionCollection(), "note_ungroup" );

	m_actMoveOnTop    = new KAction( i18n("Move on &Top"),    "2uparrow",   "Ctrl+Shift+Home",
									 this, SLOT(moveOnTop()),    actionCollection(), "note_move_top" );
	m_actMoveNoteUp   = new KAction( i18n("Move &Up"),        "1uparrow",   "Ctrl+Shift+Up",
									 this, SLOT(moveNoteUp()),   actionCollection(), "note_move_up" );
	m_actMoveNoteDown = new KAction( i18n("Move &Down"),      "1downarrow", "Ctrl+Shift+Down",
									 this, SLOT(moveNoteDown()), actionCollection(), "note_move_down" );
	m_actMoveOnBottom = new KAction( i18n("Move on &Bottom"), "2downarrow", "Ctrl+Shift+End",
									 this, SLOT(moveOnBottom()), actionCollection(), "note_move_bottom" );
#if KDE_IS_VERSION( 3, 1, 90 ) // KDE 3.2.x
	m_actPaste = KStdAction::pasteText( this, SLOT(pasteInCurrentBasket()), actionCollection() );
#else
	m_actPaste = KStdAction::paste(     this, SLOT(pasteInCurrentBasket()), actionCollection() );
#endif

	/** Insert : **************************************************************/

	QSignalMapper *insertEmptyMapper  = new QSignalMapper(this);
	QSignalMapper *insertWizardMapper = new QSignalMapper(this);
	connect( insertEmptyMapper,  SIGNAL(mapped(int)), this, SLOT(insertEmpty(int))  );
	connect( insertWizardMapper, SIGNAL(mapped(int)), this, SLOT(insertWizard(int)) );

	m_actInsertText   = new KAction( i18n("&Text"),      "text",     "Ctrl+T", actionCollection(), "insert_text"     );
	m_actInsertHtml   = new KAction( i18n("&Rich Text"), "html",     "Insert", actionCollection(), "insert_html"     );
	m_actInsertLink   = new KAction( i18n("&Link"),      "link",     "Ctrl+Y", actionCollection(), "insert_link"     );
	m_actInsertImage  = new KAction( i18n("&Image"),     "image",    "",       actionCollection(), "insert_image"    );
	m_actInsertColor  = new KAction( i18n("&Color"),     "colorset", "",       actionCollection(), "insert_color"    );
	m_actInsertLauncher=new KAction( i18n("L&auncher"),  "launch",   "",       actionCollection(), "insert_launcher" );

	m_actImportKMenu  = new KAction( i18n("Import Launcher from &KDE Menu..."), "kmenu",      "", actionCollection(), "insert_kmenu"     );
	m_actImportIcon   = new KAction( i18n("Im&port Icon..."),                   "icons",      "", actionCollection(), "insert_icon"      );
	m_actLoadFile     = new KAction( i18n("Load From &File..."),                "fileimport", "", actionCollection(), "insert_from_file" );

	connect( m_actInsertText,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertHtml,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertImage,    SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertLink,     SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertColor,    SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	connect( m_actInsertLauncher, SIGNAL(activated()), insertEmptyMapper, SLOT(map()) );
	insertEmptyMapper->setMapping(m_actInsertText,     NoteType::Text    );
	insertEmptyMapper->setMapping(m_actInsertHtml,     NoteType::Html    );
	insertEmptyMapper->setMapping(m_actInsertImage,    NoteType::Image   );
	insertEmptyMapper->setMapping(m_actInsertLink,     NoteType::Link    );
	insertEmptyMapper->setMapping(m_actInsertColor,    NoteType::Color   );
	insertEmptyMapper->setMapping(m_actInsertLauncher, NoteType::Launcher);

	connect( m_actImportKMenu, SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	connect( m_actImportIcon,  SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	connect( m_actLoadFile,    SIGNAL(activated()), insertWizardMapper, SLOT(map()) );
	insertWizardMapper->setMapping(m_actImportKMenu,  1 );
	insertWizardMapper->setMapping(m_actImportIcon,   2 );
	insertWizardMapper->setMapping(m_actLoadFile,     3 );

	m_colorPicker = new DesktopColorPicker();
	m_actColorPicker = new KAction( i18n("C&olor from Screen"), "kcolorchooser", "",
									this, SLOT(slotColorFromScreen()), actionCollection(), "insert_screen_color" );
	connect( m_colorPicker, SIGNAL(pickedColor(const QColor&)), this, SLOT(colorPicked(const QColor&)) );
	connect( m_colorPicker, SIGNAL(canceledPick()),             this, SLOT(colorPickingCanceled())     );

	m_actGrabScreenshot = new KAction( i18n("Grab Screen &Zone"), "ksnapshot", "",
									   this, SLOT(grabScreenshot()), actionCollection(), "insert_screen_capture" );
	//connect( m_actGrabScreenshot, SIGNAL(regionGrabbed(const QPixmap&)), this, SLOT(screenshotGrabbed(const QPixmap&)) );
	//connect( m_colorPicker, SIGNAL(canceledPick()),             this, SLOT(colorPickingCanceled())     );

	m_insertActions.append( m_actInsertText     );
	m_insertActions.append( m_actInsertHtml     );
	m_insertActions.append( m_actInsertLink     );
	m_insertActions.append( m_actInsertImage    );
	m_insertActions.append( m_actInsertColor    );
	m_insertActions.append( m_actImportKMenu    );
	m_insertActions.append( m_actInsertLauncher );
	m_insertActions.append( m_actImportIcon     );
	m_insertActions.append( m_actLoadFile       );
	m_insertActions.append( m_actColorPicker    );
	m_insertActions.append( m_actGrabScreenshot );

	/** Basket : **************************************************************/

	actNewBasket        = new KAction( i18n("&New Basket..."), "filenew", KStdAccel::shortcut(KStdAccel::New),
									   this, SLOT(askNewBasket()), actionCollection(), "basket_new" );
	actNewSubBasket     = new KAction( i18n("New &Sub-Basket..."), "", "Ctrl+Shift+N",
									   this, SLOT(askNewSubBasket()), actionCollection(), "basket_new_sub" );
	actNewSiblingBasket = new KAction( i18n("New Si&bling Basket..."), "", "",
									   this, SLOT(askNewSiblingBasket()), actionCollection(), "basket_new_sibling" );

	KActionMenu *newBasketMenu = new KActionMenu(i18n("&New"), "filenew", actionCollection(), "basket_new_menu");
	newBasketMenu->insert(actNewBasket);
	newBasketMenu->insert(actNewSubBasket);
	newBasketMenu->insert(actNewSiblingBasket);
	connect( newBasketMenu, SIGNAL(activated()), this, SLOT(askNewBasket()) );

	m_actPropBasket = new KAction( i18n("&Properties..."), "misc", "F2",
								   this, SLOT(propBasket()), actionCollection(), "basket_properties" );
	m_actDelBasket  = new KAction( i18n("Remove Basket", "&Remove"), "", 0,
								   this, SLOT(delBasket()), actionCollection(), "basket_remove" );
#ifdef HAVE_LIBGPGME
	m_actPassBasket = new KAction( i18n("Password protection", "&Password..."), "", 0,
								   this, SLOT(password()), actionCollection(), "basket_password" );
	m_actLockBasket = new KAction( i18n("Lock Basket", "&Lock"), "", 0,
								   this, SLOT(lockBasket()), actionCollection(), "basket_lock" );
#endif
	/** Edit : ****************************************************************/

	//m_actUndo     = KStdAction::undo(  this, SLOT(undo()),                 actionCollection() );
	//m_actUndo->setEnabled(false); // Not yet implemented !
	//m_actRedo     = KStdAction::redo(  this, SLOT(redo()),                 actionCollection() );
	//m_actRedo->setEnabled(false); // Not yet implemented !

	m_actShowFilter  = new KToggleAction( i18n("&Filter"), "filter", KStdAccel::shortcut(KStdAccel::Find),
										  actionCollection(), "edit_filter" );
	connect( m_actShowFilter, SIGNAL(toggled(bool)), this, SLOT(showHideFilterBar(bool)) );

	m_actFilterAllBaskets = new KToggleAction( i18n("Filter all &Baskets"), "find", "Ctrl+Shift+F",
											   actionCollection(), "edit_filter_all_baskets" );
	connect( m_actFilterAllBaskets, SIGNAL(toggled(bool)), this, SLOT(toggleFilterAllBaskets(bool)) );

	m_actResetFilter = new KAction( i18n( "&Reset Filter" ), "locationbar_erase", "Ctrl+R",
									this, SLOT( slotResetFilter() ), actionCollection(), "edit_filter_reset" );

	/** Go : ******************************************************************/

	m_actPreviousBasket = new KAction( i18n( "&Previous Basket" ), "up",      "Alt+Up",
									   this, SLOT(goToPreviousBasket()), actionCollection(), "go_basket_previous" );
	m_actNextBasket     = new KAction( i18n( "&Next Basket" ),     "down",    "Alt+Down",
									   this, SLOT(goToNextBasket()),     actionCollection(), "go_basket_next"     );
	m_actFoldBasket     = new KAction( i18n( "&Fold Basket" ),     "back",    "Alt+Left",
									   this, SLOT(foldBasket()),         actionCollection(), "go_basket_fold"     );
	m_actExpandBasket   = new KAction( i18n( "&Expand Basket" ),   "forward", "Alt+Right",
									   this, SLOT(expandBasket()),       actionCollection(), "go_basket_expand"   );
	// FOR_BETA_PURPOSE:
	m_convertTexts = new KAction( i18n("Convert text notes to rich text notes"), "compfile", "",
								  this, SLOT(convertTexts()), actionCollection(), "beta_convert_texts" );

	InlineEditors::instance()->initToolBars(actionCollection());
}

QListViewItem* BNPView::firstListViewItem()
{
	return m_tree->firstChild();
}

void BNPView::slotShowProperties(QListViewItem *item, const QPoint&, int)
{
	if (item)
		propBasket();
}

void BNPView::slotMouseButtonPressed(int button, QListViewItem *item, const QPoint &/*pos*/, int /*column*/)
{
	if (item && (button & Qt::MidButton)) {
		// TODO: Paste into ((BasketListViewItem*)listViewItem)->basket()
	}
}

void BNPView::slotContextMenu(KListView */*listView*/, QListViewItem *item, const QPoint &pos)
{
	QString menuName;
	if (item) {
		Basket* basket = ((BasketListViewItem*)item)->basket();

		setCurrentBasket(basket);
		menuName = "basket_popup";
	} else {
		menuName = "tab_bar_popup";
		/*
		* "File -> New" create a new basket with the same parent basket as the the current one.
		* But when invoked when right-clicking the empty area at the bottom of the basket tree,
		* it is obvious the user want to create a new basket at the bottom of the tree (with no parent).
		* So we set a temporary variable during the time the popup menu is shown,
		 * so the slot askNewBasket() will do the right thing:
		*/
		setNewBasketPopup();
	}

	QPopupMenu *menu = popupMenu(menuName);
	connect( menu, SIGNAL(aboutToHide()),  this, SLOT(aboutToHideNewBasketPopup()) );
	menu->exec(pos);
}

void BNPView::save()
{
	DEBUG_WIN << "Basket Tree: Saving...";

	// Create Document:
	QDomDocument document("basketTree");
	QDomElement root = document.createElement("basketTree");
	document.appendChild(root);

	// Save Basket Tree:
	save(m_tree->firstChild(), document, root);

	// Write to Disk:
	Basket::safelySaveToFile(Global::basketsFolder() + "baskets.xml", "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n" + document.toString());
// 	QFile file(Global::basketsFolder() + "baskets.xml");
// 	if (file.open(IO_WriteOnly)) {
// 		QTextStream stream(&file);
// 		stream.setEncoding(QTextStream::UnicodeUTF8);
// 		QString xml = document.toString();
// 		stream << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
// 		stream << xml;
// 		file.close();
// 	}
}

void BNPView::save(QListViewItem *firstItem, QDomDocument &document, QDomElement &parentElement)
{
	QListViewItem *item = firstItem;
	while (item) {
		Basket *basket = ((BasketListViewItem*)item)->basket();
		QDomElement basketElement = document.createElement("basket");
		parentElement.appendChild(basketElement);
		// Save Attributes:
		basketElement.setAttribute("folderName", basket->folderName());
		if (item->firstChild()) // If it can be expanded/folded:
			basketElement.setAttribute("folded", XMLWork::trueOrFalse(!item->isOpen()));
		if (((BasketListViewItem*)item)->isCurrentBasket())
			basketElement.setAttribute("lastOpened", "true");
		// Save Properties:
		QDomElement properties = document.createElement("properties");
		basketElement.appendChild(properties);
		basket->saveProperties(document, properties);
		// Save Child Basket:
		if (item->firstChild())
			save(item->firstChild(), document, basketElement);
		// Next Basket:
		item = item->nextSibling();
	}
}

void BNPView::load()
{
	QDomDocument *doc = XMLWork::openFile("basketTree", Global::basketsFolder() + "baskets.xml");
	//BEGIN Compatibility with 0.6.0 Pre-Alpha versions:
	if (!doc)
		doc = XMLWork::openFile("basketsTree", Global::basketsFolder() + "baskets.xml");
	//END
	if (doc != 0) {
		QDomElement docElem = doc->documentElement();
		load(m_tree, 0L, docElem);
	}
	m_loading = false;
}

void BNPView::load(KListView */*listView*/, QListViewItem *item, const QDomElement &baskets)
{
	QDomNode n = baskets.firstChild();
	while ( ! n.isNull() ) {
		QDomElement element = n.toElement();
		if ( (!element.isNull()) && element.tagName() == "basket" ) {
			QString folderName = element.attribute("folderName");
			if (!folderName.isEmpty()) {
				Basket *basket = loadBasket(folderName);
				BasketListViewItem *basketItem = appendBasket(basket, item);
				basketItem->setOpen(!XMLWork::trueOrFalse(element.attribute("folded", "false"), false));
				basket->loadProperties(XMLWork::getElement(element, "properties"));
				if (XMLWork::trueOrFalse(element.attribute("lastOpened", element.attribute("lastOpenned", "false")), false)) // Compat with 0.6.0-Alphas
					setCurrentBasket(basket);
				// Load Sub-baskets:
				load(/*(QListView*)*/0L, basketItem, element);
			}
		}
		n = n.nextSibling();
	}
}

Basket* BNPView::loadBasket(const QString &folderName)
{
	if (folderName.isEmpty())
		return 0;

	DecoratedBasket *decoBasket = new DecoratedBasket(m_stack, folderName);
	Basket          *basket     = decoBasket->basket();
	m_stack->addWidget(decoBasket);
	connect( basket, SIGNAL(countsChanged(Basket*)), this, SLOT(countsChanged(Basket*)) );
	// Important: Create listViewItem and connect signal BEFORE loadProperties(), so we get the listViewItem updated without extra work:
	connect( basket, SIGNAL(propertiesChanged(Basket*)), this, SLOT(updateBasketListViewItem(Basket*)) );

	connect( basket->decoration()->filterBar(), SIGNAL(newFilter(const FilterData&)), this, SLOT(newFilterFromFilterBar()) );

	return basket;
}

int BNPView::basketCount(QListViewItem *parent)
{
	int count = 0;

	QListViewItem *item = (parent ? parent->firstChild() : m_tree->firstChild());
	while (item) {
		count += 1 + basketCount(item);
		item = item->nextSibling();
	}

	return count;
}

bool BNPView::canFold()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (!item)
		return false;
	return item->parent() || (item->firstChild() && item->isOpen());
}

bool BNPView::canExpand()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (!item)
		return false;
	return item->firstChild();
}

BasketListViewItem* BNPView::appendBasket(Basket *basket, QListViewItem *parentItem)
{
	BasketListViewItem *newBasketItem;
	if (parentItem)
		newBasketItem = new BasketListViewItem(parentItem, ((BasketListViewItem*)parentItem)->lastChild(), basket);
	else {
		QListViewItem *child     = m_tree->firstChild();
		QListViewItem *lastChild = 0;
		while (child) {
			lastChild = child;
			child = child->nextSibling();
		}
		newBasketItem = new BasketListViewItem(m_tree, lastChild, basket);
	}

	emit basketNumberChanged(basketCount());

	return newBasketItem;
}

void BNPView::loadNewBasket(const QString &folderName, const QDomElement &properties, Basket *parent)
{
	Basket *basket = loadBasket(folderName);
	appendBasket(basket, (basket ? listViewItemForBasket(parent) : 0));
	basket->loadProperties(properties);
	setCurrentBasket(basket);
//	save();
}

BasketListViewItem* BNPView::lastListViewItem()
{
	QListViewItem *child     = m_tree->firstChild();
	QListViewItem *lastChild = 0;
	// Set lastChild to the last primary child of the list view:
	while (child) {
		lastChild = child;
		child = child->nextSibling();
	}
	// If this child have child(s), recursivly browse through them to find the real last one:
	while (lastChild && lastChild->firstChild()) {
		child = lastChild->firstChild();
		while (child) {
			lastChild = child;
			child = child->nextSibling();
		}
	}
	return (BasketListViewItem*)lastChild;
}

void BNPView::goToPreviousBasket()
{
	if (!m_tree->firstChild())
		return;

	BasketListViewItem *item     = listViewItemForBasket(currentBasket());
	BasketListViewItem *toSwitch = item->shownItemAbove();
	if (!toSwitch) {
		toSwitch = lastListViewItem();
		if (toSwitch && !toSwitch->isShown())
			toSwitch = toSwitch->shownItemAbove();
	}

	if (toSwitch)
		setCurrentBasket(toSwitch->basket());

	if (Settings::usePassivePopup())
		showPassiveContent();
}

void BNPView::goToNextBasket()
{
	if (!m_tree->firstChild())
		return;

	BasketListViewItem *item     = listViewItemForBasket(currentBasket());
	BasketListViewItem *toSwitch = item->shownItemBelow();
	if (!toSwitch)
		toSwitch = ((BasketListViewItem*)m_tree->firstChild());

	if (toSwitch)
		setCurrentBasket(toSwitch->basket());

	if (Settings::usePassivePopup())
		showPassiveContent();
}

void BNPView::foldBasket()
{
	BasketListViewItem *item = listViewItemForBasket(currentBasket());
	if (item && !item->firstChild())
		item->setOpen(false); // If Alt+Left is hitted and there is nothing to close, make sure the focus will go to the parent basket

	QKeyEvent* keyEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Left, 0, 0);
	QApplication::postEvent(m_tree, keyEvent);
}

void BNPView::expandBasket()
{
	QKeyEvent* keyEvent = new QKeyEvent(QEvent::KeyPress, Qt::Key_Right, 0, 0);
	QApplication::postEvent(m_tree, keyEvent);
}

void BNPView::closeAllEditors()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = (BasketListViewItem*)(it.current());
		item->basket()->closeEditor();
		++it;
	}
}

bool BNPView::convertTexts()
{
	bool convertedNotes = false;
	KProgressDialog dialog(
			/*parent=*/0,
			/*name=*/"",
			/*caption=*/i18n("Plain Text Notes Conversion"),
			/*text=*/i18n("Converting plain text notes to rich text ones..."),
			/*modal=*/true);
	dialog.progressBar()->setTotalSteps(basketCount());
	dialog.show(); //setMinimumDuration(50/*ms*/);

	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = (BasketListViewItem*)(it.current());
		if (item->basket()->convertTexts())
			convertedNotes = true;
		dialog.progressBar()->advance(1);
		if (dialog.wasCancelled())
			break;
		++it;
	}

	return convertedNotes;
}

/** isRunning is to avoid recursive calls because this method can be called
 * when clicking the menu action or when using the filter-bar icon... either of those calls
 * call the other to be checked... and it can cause recursive calls.
 * PS: Uggly hack? Yes, I think so :-)
 */
void BNPView::toggleFilterAllBaskets(bool doFilter)
{
	static bool isRunning = false;
	if (isRunning)
		return;
	isRunning = true;

	// Set the state:
	m_actFilterAllBaskets->setChecked(doFilter);
	//currentBasket()->decoration()->filterBar()->setFilterAll(doFilter);

//	Basket *current = currentBasket();
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->decoration()->filterBar()->setFilterAll(doFilter);
		++it;
	}

	// Protection is not necessary anymore:
	isRunning = false;

	if (doFilter)
		currentBasket()->decoration()->filterBar()->setEditFocus();

	// Filter every baskets:
	newFilter();
}

/** This function can be called recursively because we call kapp->processEvents().
 * If this function is called whereas another "instance" is running,
 * this new "instance" leave and set up a flag that is read by the first "instance"
 * to know it should re-begin the work.
 * PS: Yes, that's a very lame pseudo-threading but that works, and it's programmer-efforts cheap :-)
 */
void BNPView::newFilter()
{
	static bool alreadyEntered = false;
	static bool shouldRestart  = false;

	if (alreadyEntered) {
		shouldRestart = true;
		return;
	}
	alreadyEntered = true;
	shouldRestart  = false;

	Basket *current = currentBasket();
	const FilterData &filterData = current->decoration()->filterBar()->filterData();

	// Set the filter data for every other baskets, or reset the filter for every other baskets if we just disabled the filterInAllBaskets:
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		if (item->basket() != current)
			if (isFilteringAllBaskets())
				item->basket()->decoration()->filterBar()->setFilterData(filterData); // Set the new FilterData for every other baskets
		else
			item->basket()->decoration()->filterBar()->setFilterData(FilterData()); // We just disabled the global filtering: remove the FilterData
		++it;
	}

	// Show/hide the "little filter icons" (during basket load)
	// or the "little numbers" (to show number of found notes in the baskets) is the tree:
	m_tree->triggerUpdate();
	kapp->processEvents();

	// Load every baskets for filtering, if they are not already loaded, and if necessary:
	if (filterData.isFiltering) {
		Basket *current = currentBasket();
		QListViewItemIterator it(m_tree);
		while (it.current()) {
			BasketListViewItem *item = ((BasketListViewItem*)it.current());
			if (item->basket() != current) {
				Basket *basket = item->basket();
				if (!basket->loadingLaunched())
					basket->load();
				m_tree->triggerUpdate();
				kapp->processEvents();
				if (shouldRestart) {
					alreadyEntered = false;
					shouldRestart  = false;
					newFilter();
					return;
				}
			}
			++it;
		}
	}

	m_tree->triggerUpdate();
//	kapp->processEvents();

	alreadyEntered = false;
	shouldRestart  = false;
}

void BNPView::newFilterFromFilterBar()
{
	if (isFilteringAllBaskets())
		QTimer::singleShot(0, this, SLOT(newFilter())); // Keep time for the QLineEdit to display the filtered character and refresh correctly!
}

bool BNPView::isFilteringAllBaskets()
{
	return m_actFilterAllBaskets->isChecked();
}


BasketListViewItem* BNPView::listViewItemForBasket(Basket *basket)
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		if (item->basket() == basket)
			return item;
		++it;
	}
	return 0L;
}

Basket* BNPView::currentBasket()
{
	DecoratedBasket *decoBasket = (DecoratedBasket*)m_stack->visibleWidget();
	if (decoBasket)
		return decoBasket->basket();
	else
		return 0;
}

Basket* BNPView::parentBasketOf(Basket *basket)
{
	BasketListViewItem *item = (BasketListViewItem*)(listViewItemForBasket(basket)->parent());
	if (item)
		return item->basket();
	else
		return 0;
}

void BNPView::setCurrentBasket(Basket *basket)
{
	if (currentBasket() == basket)
		return;

	if (currentBasket())
		currentBasket()->closeEditor();

	BasketListViewItem *item = listViewItemForBasket(basket);
	if (item) {
		m_tree->setSelected(item, true);
		item->ensureVisible();
		m_stack->raiseWidget(basket->decoration());
		// If the window has changed size, only the current basket receive the event,
		// the others will receive ony one just before they are shown.
		// But this triggers unwanted animations, so we eliminate it:
		basket->relayoutNotes(/*animate=*/false);
		setCaption(item->basket()->basketName());
		countsChanged(basket);
		updateStatusBarHint();
		if (Global::tray)
			Global::tray->updateToolTip();
	}
	m_tree->viewport()->update();
	emit basketChanged();
}

void BNPView::removeBasket(Basket *basket)
{
	if (basket->isDuringEdit())
		basket->closeEditor();

	// Find a new basket to switch to and select it.
	// Strategy: get the next sibling, or the previous one if not found.
	// If there is no such one, get the parent basket:
	BasketListViewItem *basketItem = listViewItemForBasket(basket);
	BasketListViewItem *nextBasketItem = (BasketListViewItem*)(basketItem->nextSibling());
	if (!nextBasketItem)
		nextBasketItem = basketItem->prevSibling();
	if (!nextBasketItem)
		nextBasketItem = (BasketListViewItem*)(basketItem->parent());

	if (nextBasketItem)
		setCurrentBasket(nextBasketItem->basket());

	// Remove from the view:
	basket->unsubscribeBackgroundImages();
	m_stack->removeWidget(basket->decoration());
//	delete basket->decoration();
	delete basketItem;
//	delete basket;

	// If there is no basket anymore, add a new one:
	if (!nextBasketItem)
		BasketFactory::newBasket(/*icon=*/"", /*name=*/i18n("General"), /*backgroundImage=*/"", /*backgroundColor=*/QColor(), /*textColor=*/QColor(), /*templateName=*/"1column", /*createIn=*/0);
	else // No need to save two times if we add a basket
		save();

	emit basketNumberChanged(basketCount());
}

void BNPView::setTreePlacement(bool onLeft)
{
	if (onLeft)
		moveToFirst(m_tree);
	else
		moveToLast(m_tree);
	//updateGeometry();
	kapp->postEvent( this, new QResizeEvent(size(), size()) );
}

void BNPView::relayoutAllBaskets()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		//item->basket()->unbufferizeAll();
		item->basket()->unsetNotesWidth();
		item->basket()->relayoutNotes(true);
		++it;
	}
}

void BNPView::linkLookChanged()
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item = ((BasketListViewItem*)it.current());
		item->basket()->linkLookChanged();
		++it;
	}
}

void BNPView::filterPlacementChanged(bool onTop)
{
	QListViewItemIterator it(m_tree);
	while (it.current()) {
		BasketListViewItem *item        = static_cast<BasketListViewItem*>(it.current());
		DecoratedBasket    *decoration  = static_cast<DecoratedBasket*>(item->basket()->parent());
		decoration->setFilterBarPosition(onTop);
		++it;
	}
}

void BNPView::updateBasketListViewItem(Basket *basket)
{
	BasketListViewItem *item = listViewItemForBasket(basket);
	if (item)
		item->setup();

	if (basket == currentBasket()) {
		setCaption(basket->basketName());
		if (Global::tray)
			Global::tray->updateToolTip();
	}

	// Don't save if we are loading!
	if (!m_loading)
		save();
}

void BNPView::needSave(QListViewItem*)
{
	if (!m_loading)
		// A basket has been collapsed/expanded or a new one is select: this is not urgent:
		QTimer::singleShot(500/*ms*/, this, SLOT(save()));
}

void BNPView::slotPressed(QListViewItem *item, const QPoint &/*pos*/, int /*column*/)
{
	// Impossible to Select no Basket:
	if (!item)
		m_tree->setSelected(listViewItemForBasket(currentBasket()), true);
	else if (currentBasket() != ((BasketListViewItem*)item)->basket()) {
		setCurrentBasket( ((BasketListViewItem*)item)->basket() );
		needSave(0);
	}
	currentBasket()->setFocus();
}

DecoratedBasket* BNPView::currentDecoratedBasket()
{
	if (currentBasket())
		return currentBasket()->decoration();
	else
		return 0;
}

// Redirected actions :

void BNPView::exportToHTML()              { currentBasket()->exportToHTML();         }
void BNPView::editNote()                  { currentBasket()->noteEdit();             }
void BNPView::cutNote()                   { currentBasket()->noteCut();              }
void BNPView::copyNote()                  { currentBasket()->noteCopy();             }
void BNPView::delNote()                   { currentBasket()->noteDelete();           }
void BNPView::openNote()                  { currentBasket()->noteOpen();             }
void BNPView::openNoteWith()              { currentBasket()->noteOpenWith();         }
void BNPView::saveNoteAs()                { currentBasket()->noteSaveAs();           }
void BNPView::noteGroup()                 { currentBasket()->noteGroup();            }
void BNPView::noteUngroup()               { currentBasket()->noteUngroup();          }
void BNPView::moveOnTop()                 { currentBasket()->noteMoveOnTop();        }
void BNPView::moveOnBottom()              { currentBasket()->noteMoveOnBottom();     }
void BNPView::moveNoteUp()                { currentBasket()->noteMoveNoteUp();       }
void BNPView::moveNoteDown()              { currentBasket()->noteMoveNoteDown();     }
void BNPView::slotSelectAll()             { currentBasket()->selectAll();            }
void BNPView::slotUnselectAll()           { currentBasket()->unselectAll();          }
void BNPView::slotInvertSelection()       { currentBasket()->invertSelection();      }
void BNPView::slotResetFilter()           { currentDecoratedBasket()->resetFilter(); }

void BNPView::importKJots()       { SoftwareImporters::importKJots();       }
void BNPView::importKNotes()      { SoftwareImporters::importKNotes();      }
void BNPView::importKnowIt()      { SoftwareImporters::importKnowIt();      }
void BNPView::importTuxCards()    { SoftwareImporters::importTuxCards();    }
void BNPView::importStickyNotes() { SoftwareImporters::importStickyNotes(); }
void BNPView::importTomboy()      { SoftwareImporters::importTomboy();      }

void BNPView::countsChanged(Basket *basket)
{
	if (basket == currentBasket())
		notesStateChanged();
}

void BNPView::notesStateChanged()
{
	Basket *basket = currentBasket();

	// Update statusbar message :
	if (currentBasket()->isLocked())
		setSelectionStatus(i18n("Locked"));
	else if (!basket->isLoaded())
		setSelectionStatus(i18n("Loading..."));
	else if (basket->count() == 0)
		setSelectionStatus(i18n("No notes"));
	else {
		QString count     = i18n("%n note",     "%n notes",    basket->count()         );
		QString selecteds = i18n("%n selected", "%n selected", basket->countSelecteds());
		QString showns    = (currentDecoratedBasket()->filterData().isFiltering ? i18n("all matches") : i18n("no filter"));
		if (basket->countFounds() != basket->count())
			showns = i18n("%n match", "%n matches", basket->countFounds());
		setSelectionStatus(
				i18n("e.g. '18 notes, 10 matches, 5 selected'", "%1, %2, %3").arg(count, showns, selecteds) );
	}

	// If we added a note that match the global filter, update the count number in the tree:
	if (isFilteringAllBaskets())
		listViewItemForBasket(basket)->listView()->triggerUpdate();

	if (currentBasket()->redirectEditActions()) {
		m_actSelectAll         ->setEnabled( !currentBasket()->selectedAllTextInEditor() );
		m_actUnselectAll       ->setEnabled( currentBasket()->hasSelectedTextInEditor()  );
	} else {
		m_actSelectAll         ->setEnabled( basket->countSelecteds() < basket->countFounds() );
		m_actUnselectAll       ->setEnabled( basket->countSelecteds() > 0                     );
	}
	m_actInvertSelection   ->setEnabled( basket->countFounds() > 0 );

	updateNotesActions();
}

void BNPView::updateNotesActions()
{
	bool isLocked             = currentBasket()->isLocked();
	bool oneSelected          = currentBasket()->countSelecteds() == 1;
	bool oneOrSeveralSelected = currentBasket()->countSelecteds() >= 1;
	bool severalSelected      = currentBasket()->countSelecteds() >= 2;

	// FIXME: m_actCheckNotes is also modified in void BNPView::areSelectedNotesCheckedChanged(bool checked)
	//        bool Basket::areSelectedNotesChecked() should return false if bool Basket::showCheckBoxes() is false
//	m_actCheckNotes->setChecked( oneOrSeveralSelected &&
//	                             currentBasket()->areSelectedNotesChecked() &&
//	                             currentBasket()->showCheckBoxes()             );

	Note *selectedGroup = (severalSelected ? currentBasket()->selectedGroup() : 0);

	m_actEditNote            ->setEnabled( !isLocked && oneSelected && !currentBasket()->isDuringEdit() );
	if (currentBasket()->redirectEditActions()) {
		m_actCutNote         ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
		m_actCopyNote        ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
		m_actPaste           ->setEnabled( true                                       );
		m_actDelNote         ->setEnabled( currentBasket()->hasSelectedTextInEditor() );
	} else {
		m_actCutNote         ->setEnabled( !isLocked && oneOrSeveralSelected );
		m_actCopyNote        ->setEnabled(              oneOrSeveralSelected );
		m_actPaste           ->setEnabled( !isLocked                         );
		m_actDelNote         ->setEnabled( !isLocked && oneOrSeveralSelected );
	}
	m_actOpenNote        ->setEnabled(              oneOrSeveralSelected );
	m_actOpenNoteWith    ->setEnabled(              oneSelected          ); // TODO: oneOrSeveralSelected IF SAME TYPE
	m_actSaveNoteAs      ->setEnabled(              oneSelected          ); // IDEM?
	m_actGroup           ->setEnabled( !isLocked && !selectedGroup );
	m_actUngroup         ->setEnabled( !isLocked &&  selectedGroup && !selectedGroup->isColumn());
	m_actMoveOnTop       ->setEnabled( !isLocked && oneOrSeveralSelected );
	m_actMoveNoteUp      ->setEnabled( !isLocked && oneOrSeveralSelected ); // TODO: Disable when unavailable!
	m_actMoveNoteDown    ->setEnabled( !isLocked && oneOrSeveralSelected );
	m_actMoveOnBottom    ->setEnabled( !isLocked && oneOrSeveralSelected );

	for (KAction *action = m_insertActions.first(); action; action = m_insertActions.next())
		action->setEnabled( !isLocked );

	// From the old Note::contextMenuEvent(...) :
/*	if (useFile() || m_type == Link) {
	m_type == Link ? i18n("&Open target")         : i18n("&Open")
	m_type == Link ? i18n("Open target &with...") : i18n("Open &with...")
	m_type == Link ? i18n("&Save target as...")   : i18n("&Save a copy as...")
		// If useFile() theire is always a file to open / open with / save, but :
	if (m_type == Link) {
			if (url().prettyURL().isEmpty() && runCommand().isEmpty())     // no URL nor runCommand :
	popupMenu->setItemEnabled(7, false);                       //  no possible Open !
			if (url().prettyURL().isEmpty())                               // no URL :
	popupMenu->setItemEnabled(8, false);                       //  no possible Open with !
			if (url().prettyURL().isEmpty() || url().path().endsWith("/")) // no URL or target a folder :
	popupMenu->setItemEnabled(9, false);                       //  not possible to save target file
}
} else if (m_type != Color) {
	popupMenu->insertSeparator();
	popupMenu->insertItem( SmallIconSet("filesaveas"), i18n("&Save a copy as..."), this, SLOT(slotSaveAs()), 0, 10 );
}*/
}

// BEGIN Color picker (code from KColorEdit):

/* Activate the mode
 */
void BNPView::slotColorFromScreen(bool global)
{
	m_colorPickWasGlobal = global;
	if (isActiveWindow()) {
		if(Global::mainWindow()) Global::mainWindow()->hide();
		m_colorPickWasShown = true;
	} else
		m_colorPickWasShown = false;

		currentBasket()->saveInsertionData();
		m_colorPicker->pickColor();

/*	m_gettingColorFromScreen = true;
		kapp->processEvents();
		QTimer::singleShot( 100, this, SLOT(grabColorFromScreen()) );*/
}

void BNPView::slotColorFromScreenGlobal()
{
	slotColorFromScreen(true);
}

void BNPView::colorPicked(const QColor &color)
{
	if (!currentBasket()->isLoaded()) {
		showPassiveLoading(currentBasket());
		currentBasket()->load();
	}
	currentBasket()->insertColor(color);

	if (m_colorPickWasShown)
		if(Global::mainWindow()) Global::mainWindow()->show();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Picked color to basket <i>%1</i>"));
}

void BNPView::colorPickingCanceled()
{
	if (m_colorPickWasShown)
		if(Global::mainWindow()) Global::mainWindow()->show();
}

void BNPView::slotConvertTexts()
{
	int result = KMessageBox::questionYesNoCancel(
			this,
	i18n(
			"<p>This will convert every text notes into rich text notes.<br>"
			"The content of the notes will not change and you will be able to apply formating to those notes.</p>"
			"<p>This process cannot be reverted back: you will not be able to convert the rich text notes to plain text ones later.</p>"
			"<p>As a beta-tester, you are strongly encouraged to do the convert process because it is to test if plain text notes are still needed.<br>"
			"If nobody complain about not having plain text notes anymore, then the final version is likely to not support plain text notes anymore.</p>"
			"<p><b>Which basket notes do you want to convert?</b></p>"
		),
	i18n("Convert Text Notes"),
	KGuiItem(i18n("Only in the Current Basket")),
	KGuiItem(i18n("In Every Baskets"))
												 );
	if (result == KMessageBox::Cancel)
		return;

	// TODO: Please wait. This can take several minutes.

	bool conversionsDone;
	if (result == KMessageBox::Yes)
		conversionsDone = currentBasket()->convertTexts();
	else
		conversionsDone = convertTexts();

	if (conversionsDone)
		KMessageBox::information(this, i18n("The text notes have been converted to rich text ones."), i18n("Conversion Finished"));
	else
		KMessageBox::information(this, i18n("There is no text notes to convert."), i18n("Conversion Finished"));
}

QPopupMenu* BNPView::popupMenu(const QString &menuName)
{
	QPopupMenu *menu = (QPopupMenu *)m_guiClient->factory()->container(menuName, m_guiClient);
	if (menu == 0) {
		KStandardDirs stdDirs;
		KMessageBox::error( this, i18n(
				"<p><b>The file basketui.rc seems to not exist or is too old.<br>"
						"%1 cannot run without it and will stop.</b></p>"
						"<p>Please check your installation of %2.</p>"
						"<p>If you haven't administrator access to install the application "
						"system wide, you can copy the file basketui.rc from the installation "
						"archive to the folder <a href='file://%3'>%4</a>.</p>"
						"<p>In last ressort, if you are sure the application is well installed "
						"but you had a preview version of it, try to remove the "
						"file %5basketui.rc</p>")
						.arg(kapp->aboutData()->programName(), kapp->aboutData()->programName(),
							 stdDirs.saveLocation("data", "basket/")).arg(stdDirs.saveLocation("data", "basket/"), stdDirs.saveLocation("data", "basket/")),
				i18n("Ressource not Found"), KMessageBox::AllowLink );
		// This exits Kontact so it cannot be done anymore
		// exit(1); // We SHOULD exit right now and abord everything because the caller except menu != 0 to not crash.
		menu = new QPopupMenu;
	}
	return menu;
}

void BNPView::showHideFilterBar(bool show, bool switchFocus)
{
//	if (show != m_actShowFilter->isChecked())
//		m_actShowFilter->setChecked(show);
	m_actShowFilter->setChecked(currentDecoratedBasket()->filterData().isFiltering);

	currentDecoratedBasket()->setFilterBarShown(show, switchFocus);
	currentDecoratedBasket()->resetFilter();
}

void BNPView::insertEmpty(int type)
{
	if (currentBasket()->isLocked()) {
		showPassiveImpossible(i18n("Cannot add note."));
		return;
	}
	currentBasket()->insertEmptyNote(type);
}

void BNPView::insertWizard(int type)
{
	if (currentBasket()->isLocked()) {
		showPassiveImpossible(i18n("Cannot add note."));
		return;
	}
	currentBasket()->insertWizard(type);
}

// BEGIN Screen Grabbing: // FIXME

void BNPView::grabScreenshot(bool global)
{
	if (m_regionGrabber) {
		KWin::activateWindow(m_regionGrabber->winId());
		return;
	}

	// Delay before to take a screenshot because if we hide the main window OR the systray popup menu,
	// we should wait the windows below to be repainted!!!
	// A special case is where the action is triggered with the global keyboard shortcut.
	// In this case, global is true, and we don't wait.
	// In the future, if global is also defined for other cases, check for
	// enum KAction::ActivationReason { UnknownActivation, EmulatedActivation, AccelActivation, PopupMenuActivation, ToolBarActivation };
	int delay = (isActiveWindow() ? 500 : (global/*kapp->activePopupWidget()*/ ? 0 : 200));

	m_colorPickWasGlobal = global;
	if (isActiveWindow()) {
		if(Global::mainWindow()) Global::mainWindow()->hide();
		m_colorPickWasShown = true;
	} else
		m_colorPickWasShown = false;

		currentBasket()->saveInsertionData();
		m_regionGrabber = new RegionGrabber(delay);
		connect( m_regionGrabber, SIGNAL(regionGrabbed(const QPixmap&)), this, SLOT(screenshotGrabbed(const QPixmap&)) );
}

void BNPView::grabScreenshotGlobal()
{
	grabScreenshot(true);
}

void BNPView::screenshotGrabbed(const QPixmap &pixmap)
{
	delete m_regionGrabber;
	m_regionGrabber = 0;

	// Cancelled (pressed Escape):
	if (pixmap.isNull()) {
		if (m_colorPickWasShown)
			if(Global::mainWindow()) Global::mainWindow()->show();
		return;
	}

	if (!currentBasket()->isLoaded()) {
		showPassiveLoading(currentBasket());
		currentBasket()->load();
	}
	currentBasket()->insertImage(pixmap);

	if (m_colorPickWasShown)
		if(Global::mainWindow()) Global::mainWindow()->show();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Grabbed screen zone to basket <i>%1</i>"));
}

Basket* BNPView::basketForFolderName(const QString &/*folderName*/)
{
/*	QPtrList<Basket> basketsList = listBaskets();
	Basket *basket;
	for (basket = basketsList.first(); basket; basket = basketsList.next())
	if (basket->folderName() == folderName)
	return basket;
*/
	return 0;
}

void BNPView::setFiltering(bool filtering)
{
	m_actShowFilter->setChecked(filtering);
	m_actResetFilter->setEnabled(filtering);
}

void BNPView::undo()
{
	// TODO
}

void BNPView::redo()
{
	// TODO
}

void BNPView::pasteToBasket(int /*index*/, QClipboard::Mode /*mode*/)
{
	//TODO: REMOVE!
	//basketAt(index)->pasteNote(mode);
}

void BNPView::propBasket()
{
	BasketPropertiesDialog dialog(currentBasket(), this);
	dialog.exec();
}

void BNPView::delBasket()
{
//	DecoratedBasket *decoBasket    = currentDecoratedBasket();
	Basket          *basket        = currentBasket();

#if 0
	KDialogBase *dialog = new KDialogBase(this, /*name=*/0, /*modal=*/true, /*caption=*/i18n("Delete Basket"),
										  KDialogBase::User1 | KDialogBase::User2 | KDialogBase::No, KDialogBase::User1,
										 /*separator=*/false,
										 /*user1=*/KGuiItem(i18n("Delete Only that Basket")/*, icon=""*/),
										 /*user2=*/KGuiItem(i18n("Delete With its Childs")/*, icon=""*/) );
	QStringList basketsList;
	basketsList.append("Basket 1");
	basketsList.append("  Basket 2");
	basketsList.append("    Basket 3");
	basketsList.append("  Basket 4");
	KMessageBox::createKMessageBox(
			dialog, QMessageBox::Information,
			i18n("<qt>Do you really want to remove <b>%1</b> and its contents?</qt>")
				.arg(Tools::textToHTMLWithoutP(basket->basketName())),
			basketsList, /*ask=*/"", /*checkboxReturn=*/0, /*options=*/KMessageBox::Notify/*, const QString &details=QString::null*/);
#endif

	int really = KMessageBox::questionYesNo( this,
											 i18n("<qt>Do you really want to remove <b>%1</b> and its contents?</qt>")
													 .arg(Tools::textToHTMLWithoutP(basket->basketName())),
											 i18n("Remove Basket")
#if KDE_IS_VERSION( 3, 2, 90 ) // KDE 3.3.x
													 , KGuiItem(i18n("&Remove Basket"), "editdelete"), KStdGuiItem::cancel());
#else
		                    );
#endif

	if (really == KMessageBox::No)
		return;

	QStringList basketsList = listViewItemForBasket(basket)->childNamesTree();
	if (basketsList.count() > 0) {
		int deleteChilds = KMessageBox::questionYesNoList( this,
				i18n("<qt><b>%1</b> have the following child baskets.<br>Do you want to remove them too?</qt>")
						.arg(Tools::textToHTMLWithoutP(basket->basketName())),
				basketsList,
				i18n("Remove Child Baskets")
#if KDE_IS_VERSION( 3, 2, 90 ) // KDE 3.3.x
						, KGuiItem(i18n("&Remove Child Baskets"), "editdelete"));
#else
		);
#endif

		if (deleteChilds == KMessageBox::No)
			listViewItemForBasket(basket)->moveChildsBaskets();
	}

	doBasketDeletion(basket);

//	basketNumberChanged();
//	rebuildBasketsMenu();
}

void BNPView::doBasketDeletion(Basket *basket)
{
	QListViewItem *basketItem = listViewItemForBasket(basket);
	QListViewItem *nextOne;
	for (QListViewItem *child = basketItem->firstChild(); child; child = nextOne) {
		nextOne = child->nextSibling();
		// First delete the child baskets:
		doBasketDeletion(((BasketListViewItem*)child)->basket());
	}
	// Then, basket have no child anymore, delete it:
	DecoratedBasket *decoBasket = basket->decoration();
	basket->deleteFiles();
	removeBasket(basket);
	delete decoBasket;
//	delete basket;
}

void BNPView::password()
{
#ifdef HAVE_LIBGPGME
	PasswordDlg dlg(this, "Password");
	Basket *cur = currentBasket();

	dlg.setType(cur->encryptionType());
	dlg.setKey(cur->encryptionKey());
	if(dlg.exec()) {
		cur->setProtection(dlg.type(), dlg.key());
		if (cur->encryptionType() != Basket::NoEncryption)
			cur->lock();
	}
#endif
}

void BNPView::lockBasket()
{
#ifdef HAVE_LIBGPGME
	Basket *cur = currentBasket();

	cur->lock();
#endif
}

void BNPView::activatedTagShortcut()
{
	Tag *tag = Tag::tagForKAction((KAction*)sender());
	currentBasket()->activatedTagShortcut(tag);
}

void BNPView::slotBasketNumberChanged(int number)
{
	m_actPreviousBasket->setEnabled(number > 1);
	m_actNextBasket    ->setEnabled(number > 1);
}

void BNPView::slotBasketChanged()
{
	m_actFoldBasket->setEnabled(canFold());
	m_actExpandBasket->setEnabled(canExpand());
	setFiltering(currentBasket() && currentBasket()->decoration()->filterData().isFiltering);
}

void BNPView::currentBasketChanged()
{
}

void BNPView::isLockedChanged()
{
	bool isLocked = currentBasket()->isLocked();

	setLockStatus(isLocked);

//	m_actLockBasket->setChecked(isLocked);
	m_actPropBasket->setEnabled(!isLocked);
	m_actDelBasket ->setEnabled(!isLocked);
	updateNotesActions();
}

void BNPView::askNewBasket()
{
	askNewBasket(0, 0);
}

void BNPView::askNewBasket(Basket *parent, Basket *pickProperties)
{
	NewBasketDefaultProperties properties;
	if (pickProperties) {
		properties.icon            = pickProperties->icon();
		properties.backgroundImage = pickProperties->backgroundImageName();
		properties.backgroundColor = pickProperties->backgroundColorSetting();
		properties.textColor       = pickProperties->textColorSetting();
		properties.freeLayout      = pickProperties->isFreeLayout();
		properties.columnCount     = pickProperties->columnsCount();
	}

	NewBasketDialog(parent, properties, this).exec();
}

void BNPView::askNewSubBasket()
{
	askNewBasket( /*parent=*/currentBasket(), /*pickPropertiesOf=*/currentBasket() );
}

void BNPView::askNewSiblingBasket()
{
	askNewBasket( /*parent=*/parentBasketOf(currentBasket()), /*pickPropertiesOf=*/currentBasket() );
}

void BNPView::globalPasteInCurrentBasket()
{
	currentBasket()->setInsertPopupMenu();
	pasteInCurrentBasket();
	currentBasket()->cancelInsertPopupMenu();
}

void BNPView::pasteInCurrentBasket()
{
	currentBasket()->pasteNote();

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Clipboard content pasted to basket <i>%1</i>"));
}

void BNPView::pasteSelInCurrentBasket()
{
	currentBasket()->pasteNote(QClipboard::Selection);

	if (Settings::usePassivePopup())
		showPassiveDropped(i18n("Selection pasted to basket <i>%1</i>"));
}

void BNPView::showPassiveDropped(const QString &title)
{
	if ( ! currentBasket()->isLocked() ) {
		// TODO: Keep basket, so that we show the message only if something was added to a NOT visible basket
		m_passiveDroppedTitle     = title;
		m_passiveDroppedSelection = currentBasket()->selectedNotes();
		QTimer::singleShot( c_delayTooltipTime, this, SLOT(showPassiveDroppedDelayed()) );
		// DELAY IT BELOW:
	} else
		showPassiveImpossible(i18n("No note was added."));
}

void BNPView::showPassiveDroppedDelayed()
{
	if (isActiveWindow())
		return;

	QString title = m_passiveDroppedTitle;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::tray : this);
	QPixmap contentsPixmap = NoteDrag::feedbackPixmap(m_passiveDroppedSelection);
	QMimeSourceFactory::defaultFactory()->setPixmap("_passivepopup_image_", contentsPixmap);
	m_passivePopup->setView(
			title.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName())),
	(contentsPixmap.isNull() ? "" : "<img src=\"_passivepopup_image_\">"),
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveImpossible(const QString &message)
{
	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::tray : (QWidget*)this);
	m_passivePopup->setView(
			QString("<font color=red>%1</font>")
			.arg(i18n("Basket <i>%1</i> is locked"))
			.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName())),
	message,
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveContentForced()
{
	showPassiveContent(/*forceShow=*/true);
}

void BNPView::showPassiveContent(bool forceShow/* = false*/)
{
	if (!forceShow && isActiveWindow())
		return;

	// FIXME: Duplicate code (2 times)
	QString message;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::tray : (QWidget*)this);
	m_passivePopup->setView(
			"<qt>" + kapp->makeStdCaption( currentBasket()->isLocked()
			? QString("%1 <font color=gray30>%2</font>")
			.arg(Tools::textToHTMLWithoutP(currentBasket()->basketName()), i18n("(Locked)"))
	: Tools::textToHTMLWithoutP(currentBasket()->basketName()) ),
	message,
	kapp->iconLoader()->loadIcon(currentBasket()->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::showPassiveLoading(Basket *basket)
{
	if (isActiveWindow())
		return;

	delete m_passivePopup; // Delete previous one (if exists): it will then hide it (only one at a time)
	m_passivePopup = new KPassivePopup(Settings::useSystray() ? (QWidget*)Global::tray : (QWidget*)this);
	m_passivePopup->setView(
			Tools::textToHTMLWithoutP(basket->basketName()),
	i18n("Loading..."),
	kapp->iconLoader()->loadIcon(basket->icon(), KIcon::NoGroup, 16, KIcon::DefaultState, 0L, true));
	m_passivePopup->show();
}

void BNPView::addNoteText()  { if(Global::mainWindow()) Global::mainWindow()->show(); currentBasket()->insertEmptyNote(NoteType::Text);  }
void BNPView::addNoteHtml()  { if(Global::mainWindow()) Global::mainWindow()->show(); currentBasket()->insertEmptyNote(NoteType::Html);  }
void BNPView::addNoteImage() { if(Global::mainWindow()) Global::mainWindow()->show(); currentBasket()->insertEmptyNote(NoteType::Image); }
void BNPView::addNoteLink()  { if(Global::mainWindow()) Global::mainWindow()->show(); currentBasket()->insertEmptyNote(NoteType::Link);  }
void BNPView::addNoteColor() { if(Global::mainWindow()) Global::mainWindow()->show(); currentBasket()->insertEmptyNote(NoteType::Color); }

void BNPView::aboutToHideNewBasketPopup()
{
	QTimer::singleShot(0, this, SLOT(cancelNewBasketPopup()));
}

void BNPView::cancelNewBasketPopup()
{
	m_newBasketPopup = false;
}

void BNPView::setNewBasketPopup()
{
	m_newBasketPopup = true;
}

void BNPView::setCaption(QString s)
{
	emit setWindowCaption(s);
}

void BNPView::updateStatusBarHint()
{
	m_statusbar->updateStatusBarHint();
}

void BNPView::setSelectionStatus(QString s)
{
	m_statusbar->setSelectionStatus(s);
}

void BNPView::setLockStatus(bool isLocked)
{
	m_statusbar->setLockStatus(isLocked);
}

void BNPView::postStatusbarMessage(const QString& msg)
{
	m_statusbar->postStatusbarMessage(msg);
}

void BNPView::setStatusBarHint(const QString &hint)
{
	m_statusbar->setStatusBarHint(hint);
}

void BNPView::setActive(bool active)
{
//	std::cout << "Main Window Position: setActive(" << (active ? "true" : "false") << ")" << std::endl;
	KMainWindow* win = Global::mainWindow();
	if(!win)
		return;

#if KDE_IS_VERSION( 3, 2, 90 )   // KDE 3.3.x
	if (active) {
		kapp->updateUserTimestamp(); // If "activate on mouse hovering systray", or "on drag throught systray"
		Global::tray->setActive();   //  FIXME: add this in the places it need
	} else
		Global::tray->setInactive();
#elif KDE_IS_VERSION( 3, 1, 90 ) // KDE 3.2.x
	// Code from Kopete (that seem to work, in waiting KSystemTray make puplic the toggleSHown) :
	if (active) {
		win->show();
		//raise() and show() should normaly deIconify the window. but it doesn't do here due
		// to a bug in Qt or in KDE  (qt3.1.x or KDE 3.1.x) then, i have to call KWin's method
		if (win->isMinimized())
			KWin::deIconifyWindow(winId());

		if ( ! KWin::windowInfo(winId(), NET::WMDesktop).onAllDesktops() )
			KWin::setOnDesktop(winId(), KWin::currentDesktop());
		win->raise();
		// Code from me: expected and correct behavviour:
		kapp->updateUserTimestamp(); // If "activate on mouse hovering systray", or "on drag throught systray"
		KWin::activateWindow(win->winId());
	} else
		win->hide();
#else                            // KDE 3.1.x and lower
	if (win->active) {
		if (win->isMinimized())
			win->hide();        // If minimized, show() doesn't work !
		win->show();            // Show it
	//		showNormal();      // If it was minimized
		win->raise();           // Raise it on top
		win->setActiveWindow(); // And set it the active window
	} else
		win->hide();
#endif
}

void BNPView::hideOnEscape()
{
	if (Settings::useSystray())
		setActive(false);
}

bool BNPView::isPart()
{
	return (strcmp(name(), "BNPViewPart") == 0);
}

void BNPView::newBasket()
{
	kdDebug() << k_funcinfo << endl;
	askNewBasket();
}

#include "bnpview.moc"
