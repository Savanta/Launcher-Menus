#ifndef GAME_MENU_H
#define GAME_MENU_H

#include <vector>
#include <cstdlib>
#include "iso.h"
#include "eboot.h"
#include "gfx.h"

#define MAX_DRAW_ENTRIES 5
#define MAX_LOADED_ICONS 11

typedef enum {
    GAME = 0,
    HOMEBREW = 1,
    POPS = 2
}EntryType;

using namespace std;

class Menu{

    public:
        EntryType type;
        int slot;       /* which on-screen column this menu is drawn in (may differ from type) */
        float slideX;   /* current horizontal offset of the whole strip, in pixels */
        int index;
        vector<Entry*>* entries;

    private:
        int threadIndex;
        int animating;
        float animState;
        float visualIndex;
        bool fastScrolling;
        bool animDelay;
        bool initLoad;
        bool stopLoading;
        void freeIcons();
        bool checkIconsNeeded(bool isSelected);
        void checkIndex();
        
    public:
        Menu(EntryType t);
        ~Menu();
        
        /* a menu can be assigned to any of the three columns */
        void setSlot(int s){ this->slot = s; }
        /* horizontal offset of the sliding strip (same value for all visible columns) */
        void setSlideX(float x){ this->slideX = x; }
        
        void draw(bool selected);
        
        void loadIconsDynamic(bool isSelected);
        
        bool waitIconsLoad(bool isSelected, bool forceQuit=false);
        
        void resumeIconLoading();
        
        void addEntry(Entry* e);
        Entry* getEntry();
        Entry* getEntry(int index);
        void clearEntries();
        bool isAnimating();
        bool empty();
        
        void animStart(int direction);
        void moveUp();
        void moveDown();
        void stopFastScroll();
};
        
#endif
