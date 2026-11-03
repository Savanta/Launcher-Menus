#ifndef GAMEMGR_H
#define GAMEMGR_H

#include <cstdlib>
#include <dirent.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include "system_entry.h"
#include "menu.h"
#include "controller.h"
#include "gfx.h"
#include "animations.h"
#include "browser.h"
#include "umd.h"

#define MAX_CATEGORIES 3

using namespace std;

enum{
    ICONS_STOPPED,
    ICONS_LOADING,
    ICONS_PAUSED,
};

class GameManager : public SystemEntry{

    private:

        GameManager();

        TextScroll scroll;
    
        /* Array of game menus */
        Menu* categories[MAX_CATEGORIES];
        
        /* ISO folder categories (folder mode, like Game Categories Lite).
           folderMenus[0] is always categories[GAME] (the "all games" list) */
        vector<Menu*> folderMenus;
        vector<string> folderNames;
        
        /* columns: one per category, drawn side by side (three visible at a time) */
        vector<Menu*> colMenus;
        vector<string> colLabels;
        float slideX;               // strip offset used while sliding to the next column
        /* the menu shown in a given on-screen slot, counted from the focused column */
        Menu* visibleMenu(int slot);
        
        /* filters read from SEPLUGINS/gclite_filter.txt (keeps launcher in sync with the XMB) */
        vector<string> hiddenCategories;
        vector<string> hiddenApps;
        
        /* favorites (paths, persisted in SEPLUGINS/arkmenu_fav.txt) */
        vector<string> favorites;
        Menu* favoritesMenu;
        
        /* recently played (paths, persisted in SEPLUGINS/arkmenu_recent.txt) */
        vector<string> recent;
        Menu* recentMenu;
        clock_t lastSlideClock;     // for the time based strip animation
        
        /* Selected game menu */
        int selectedCategory;
        int maxDraw;
        
        bool use_categories;
        
        /* current game menu (either a core category or an ISO folder category) */
        Menu* currentMenu();
        void scanFolderCategories();
        void loadGclFilters();
        bool isHiddenCategory(const string& name);
        bool isHiddenApp(const string& name);
        void loadFavorites();
        void saveFavorites();
        void applyFavorites();
        void rebuildFavoritesMenu();
        /* columns: one per category, side by side, with a sliding three-column window */
        void buildColumns();
        void clampCamera();
        void focusColumn(int index);
        void columnJumpList();
        void drawFooter();
        string visibleLabel(int slot);
        void loadRecent();
        void rebuildRecentMenu();
        
        /* frames during which button actions are ignored (right after switching category) */
        int input_lock;
        
        /* Multithreading variables */
        SceUID iconThread; // UID's of the icon thread
        SceUID iconSema; // semaphore to lock the thread when sleeping
        int dynamicIconRunning;
        bool scanning;
        volatile bool selectionPending;
        int pendingCategory;
        volatile bool umdChangePending;
        bool pendingUmdPresent;
        
        /* Screen drawing thread data */
        bool hasLoaded; // whether the main thread has finished loading or not, if not then only draw the background and animation
        
        /* Options Menu instance, will be drawn by the draw thread if it's different from null */
        OptionsMenu* optionsmenu;

        void endAllThreads();
        void applyWorkerUpdates();
        
        // Entry animation
        void animAppear();
        void animDisappear();
        
        /* find all available menu entries
            ms0:/PSP/GAME for eboots
            ms0:/ISO for ISOs
            ms0:/PSP/SAVEDATA for both
         */
        void findEntries();
        void findEboots(const char* path);
        void findISOs(const char* path, Menu* dest=NULL);

        /* move the menu in the specified direction */
        void moveLeft();
        void moveRight();
        void moveUp();
        void moveDown();
        void stopFastScroll();
        
        int getNextCategory(int current);
        int getPreviousCategory(int current);
        
        void execApp();
        void extractHomebrew();
        void gameOptionsMenu();
        void startBoot();
        
    public:
    
        ~GameManager();

        static GameManager* getInstance();
        
        /* thread to load icon0 in the background */
        static int loadIcons(SceSize _args, void *_argp);
        
        // update game list if specific path has changed
        static void updateGameList(const char* path);
        
        /* obtain the currently selected entry */
        Entry* getEntry();
        
        /* draw all three menus */
        void draw();
        
        /* control the menus */
        void control(Controller* pad);
        
        void pause();
        void resume();
        
        string getInfo();
        string getFooter();
        
        string getName(){
            return "Games";
        }

        void drawInfo(){
            string info = getInfo();
            bool is_entry_info = (getEntry() != NULL && info == getEntry()->getName());
            bool translate = ( !is_entry_info || info == "Recovery Menu" || info == "UMD Drive");
            // the source of the focused column is shown in front of the game name, in the
            // same line and on the same strip as the rest of the header text
            float x = 5;
            if (selectedCategory >= 0 && selectedCategory < (int)colLabels.size() && colLabels[selectedCategory].size() > 0){
                string label = colLabels[selectedCategory];
                // drop a leading "NN " ordering prefix: the header stays compact, the
                // picker still shows the full name
                if (label.size() > 3 && label[0] >= '0' && label[0] <= '9' &&
                    label[1] >= '0' && label[1] <= '9' && label[2] == ' ')
                    label = label.substr(3);
                string tag = "[" + label + "]";
                common::printText(x, 13, tag.c_str(), YELLOW_COLOR, SIZE_MEDIUM, 0, NULL, 0);
                x += common::calcTextWidth(tag.c_str(), SIZE_MEDIUM, 0) + 6;
            }
            common::printText(x, 13, info.c_str(), LITEGRAY, SIZE_MEDIUM, 0, &scroll, translate);
        }
        
        /* Control the icon threads */
        void pauseIcons();
        void resumeIcons();
        bool waitIconsLoad(bool forceQuit=false);
        
        void setInfo(string info){};
        void setFooter(string info){};
        void setName(string name){};
        
        Image* getIcon(){
            return common::getImage(IMAGE_GAME);
        }

        bool isStillLoading(){
            return (this->scanning);
        }

        /* Popup Menu */
        void MenuPopup();
        
        /* get a specific category menu */
        Menu* getMenu(EntryType t);
        
        static bool update_game_list;
        
};

#endif
