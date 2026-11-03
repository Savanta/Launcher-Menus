/* Game Manager class */

#include <sstream>
#include <fstream>
#include <unistd.h>
#include <algorithm>
#include <stdlib.h>
#include <malloc.h>
#include <pspiofilemgr.h>
#include <systemctrl.h>
#include "system_mgr.h"
#include "gamemgr.h"
#include "music_player.h"
#include "osk.h"
#include "lang.h"
#include "texteditor.h"

static GameManager* self = NULL;

static bool loadingData = false;

ARKConfig* ark_config;

GameManager* GameManager::getInstance(){
    if (self == NULL) self = new GameManager();
    return self;
}

void GameManager::pause(){
    while (selectedCategory == -1){
        this->applyWorkerUpdates();
        sceKernelDelayThread(10000);
    }
    for (int i=MAX_CATEGORIES; i>=0; i--){
        SystemMgr::pauseDraw();
        this->maxDraw = i;
        SystemMgr::resumeDraw();
        sceKernelDelayThread(60000);
    }
    pauseIcons();
}

void GameManager::resume(){
    for (int i=0; i<=MAX_CATEGORIES; i++){
        SystemMgr::pauseDraw();
        this->maxDraw = i;
        SystemMgr::resumeDraw();
        sceKernelDelayThread(60000);
    }
    SystemMgr::pauseDraw();
    if (selectedCategory == -2) selectedCategory = -1;
    SystemMgr::resumeDraw();
    resumeIcons();
}

GameManager::GameManager(){
    // set the global self variable as this instance for the threads to use it
    self = this;

    scroll.w = 0;
    this->use_categories = true;
    this->scanning = true;
    this->optionsmenu = NULL;

    // initialize the categories
    this->selectedCategory = (common::config.main_menu)? -2 : -1;
    this->selectionPending = false;
    this->pendingCategory = -2;
    this->umdChangePending = false;
    this->pendingUmdPresent = false;
    for (int i=0; i<MAX_CATEGORIES; i++){
        this->categories[i] = new Menu((EntryType)i);
    }

    // column sources: "" means "whatever this column shows by default"
    this->input_lock = 0;
    this->favoritesMenu = NULL;
    this->folderMenus.push_back(this->categories[GAME]);
    this->folderNames.push_back("");
    for (int i=0; i<MAX_CATEGORIES; i++){
        this->colSource[i] = "";
        this->colMenus[i] = this->categories[i];
    }

    // start the multithreaded icon loading
    this->maxDraw = MAX_CATEGORIES;
    this->dynamicIconRunning = ICONS_LOADING;
    this->iconSema = sceKernelCreateSema("icon0_sema",  0, 1, 1, NULL);
    this->iconThread = sceKernelCreateThread("icon0_thread", GameManager::loadIcons, 0x10, 0x2000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(this->iconThread,  0, NULL);
}

GameManager::~GameManager(){
    for (int i=0; i<MAX_CATEGORIES; i++){
        delete this->categories[i];
    }
    // folderMenus[0] is categories[GAME] (deleted above); the favorites menu is owned by folderMenus
    this->favoritesMenu = NULL;
    for (unsigned int i=1; i<this->folderMenus.size(); i++){
        delete this->folderMenus[i];
    }
}

int GameManager::loadIcons(SceSize _args, void *_argp){

    while (true){
        sceKernelWaitSema(self->iconSema, 1, NULL);
        if (self->dynamicIconRunning == ICONS_STOPPED){
            sceKernelSignalSema(self->iconSema, 1);
            break;
        }

        // check UMD status
        sceKernelDelayThread(0);
        std::vector<Entry*>* game_entries = self->categories[GAME]->entries;
        bool has_umd = UMD::isUMD();
        bool umd_loaded = game_entries->size() > 0 && string("UMD") == game_entries->at(0)->getType();
        if (!self->umdChangePending && self->selectedCategory >= 0 &&
            (has_umd != umd_loaded)){
            self->pendingUmdPresent = has_umd;
            self->umdChangePending = true;
        }
        // load icons
        if (self->selectedCategory < 0){
            if (self->selectedCategory == -1 && !self->selectionPending)
                self->findEntries();
            sceKernelSignalSema(self->iconSema, 1);
            sceKernelDelayThread(0);
            continue;
        }
        for (int i=0; i<MAX_CATEGORIES; i++){
            if (self->colMenus[i] != NULL)
                self->colMenus[i]->loadIconsDynamic(i == self->selectedCategory);
            sceKernelDelayThread(0);
        }
        sceKernelSignalSema(self->iconSema, 1);
    }

    sceKernelExitDeleteThread(0);
    
    return 0;
}

void GameManager::pauseIcons(){
    if (self->dynamicIconRunning == ICONS_PAUSED) return; // already paused
    sceKernelWaitSema(iconSema, 1, NULL);
    self->dynamicIconRunning = ICONS_PAUSED;
}

void GameManager::resumeIcons(){
    if (self->dynamicIconRunning == ICONS_LOADING) return; // already running
    for (int i=0; i<MAX_CATEGORIES; i++){
        if (colMenus[i] != NULL)
            colMenus[i]->resumeIconLoading();
    }
    if (favoritesMenu)
        favoritesMenu->resumeIconLoading();
    sceKernelSignalSema(iconSema, 1);
    sceKernelDelayThread(0);
    self->dynamicIconRunning = ICONS_LOADING;
}

bool GameManager::waitIconsLoad(bool forceQuit){
    int icon_status = this->dynamicIconRunning;
    if (icon_status == ICONS_LOADING)
        this->pauseIcons();

    for (int i = 0; i<MAX_CATEGORIES; i++){
        Menu* menu = (this->colMenus[i] != NULL) ? this->colMenus[i] : this->categories[i];
        if (!menu->waitIconsLoad(i == selectedCategory, forceQuit))
            return false;
    }

    if (icon_status == ICONS_LOADING)
        this->resumeIcons();
    return true;
}

Menu* GameManager::getMenu(EntryType t){
    return this->categories[(int)t];
}

void GameManager::findEntries(){

    int ms_is_ef = sctrlKernelMsIsEf();

    // load gclite_filter.txt first, so both the eboot and the ISO scan honour it
    this->loadGclFilters();

    // clear entries
    this->categories[0]->clearEntries();
    this->categories[1]->clearEntries();
    this->categories[2]->clearEntries();
    
    // scan eboots
    this->findEboots("ms0:/PSP/VHBL/");
    this->findEboots("ms0:/PSP/APPS/");
    this->findEboots("ms0:/PSP/GAME/");
    this->findEboots("ms0:/PSP/GAME150/");
    if (!ms_is_ef) {
        this->findEboots("ef0:/PSP/VHBL/");
        this->findEboots("ef0:/PSP/APPS/");
        this->findEboots("ef0:/PSP/GAME/");
    }
    // scan ISOs
    this->findISOs("ms0:/ISO/");
    if (!ms_is_ef) this->findISOs("ef0:/ISO/");

    // build the ISO folder categories (folder mode), apply favorites and resolve the columns
    this->loadFavorites();
    this->rebuildFavoritesMenu();
    this->scanFolderCategories();
    this->loadColumns();
    this->resolveColumns();
    this->applyFavorites();

    if (common::config.sort_entries){
        std::sort(this->categories[0]->entries->begin(), this->categories[0]->entries->end(), Entry::cmpEntriesForSort);
        std::sort(this->categories[1]->entries->begin(), this->categories[1]->entries->end(), Entry::cmpEntriesForSort);
        std::sort(this->categories[2]->entries->begin(), this->categories[2]->entries->end(), Entry::cmpEntriesForSort);
    }

    // find the first category with entries
    this->pendingCategory = -2;
    for (int i=0; i<MAX_CATEGORIES && pendingCategory < 0; i++){
        if (!this->categories[i]->empty())
            this->pendingCategory = i;
    }

    this->selectionPending = true;
}

void GameManager::applyWorkerUpdates(){
    if (!this->selectionPending && !this->umdChangePending)
        return;

    sceKernelWaitSema(this->iconSema, 1, NULL);
    bool umdChanged = false;
    if (this->selectionPending || this->umdChangePending){
        SystemMgr::pauseDraw();
        if (this->selectionPending){
            this->selectedCategory = this->pendingCategory;
            this->selectionPending = false;
            this->scanning = false;
        }
        if (this->umdChangePending){
            std::vector<Entry*>* game_entries = this->categories[GAME]->entries;
            if (this->pendingUmdPresent){
                if (game_entries->empty() ||
                    string("UMD") != game_entries->at(0)->getType()){
                    game_entries->insert(game_entries->begin(), new UMD());
                    umdChanged = true;
                }
            }
            else if (!game_entries->empty() &&
                     string("UMD") == game_entries->at(0)->getType()){
                UMD* umd = (UMD*)game_entries->at(0);
                game_entries->erase(game_entries->begin());
                delete umd;
                umdChanged = true;
                if (game_entries->empty() && this->selectedCategory == GAME)
                    this->selectedCategory = HOMEBREW;
            }
            this->umdChangePending = false;
        }
        SystemMgr::resumeDraw();
    }
    sceKernelSignalSema(this->iconSema, 1);
    if (umdChanged)
        common::sound_mp3->play();
}

void GameManager::findEboots(const char* path){ 

    SceIoDirent dit; memset(&dit, 0, sizeof(dit));
    SceUID dir = sceIoDopen(path);
    
    if (dir < 0)
        return;
        
    while (sceIoDread(dir, &dit) > 0){
        if (strstr(dit.d_name, "%") != NULL) continue; // ignore 1.50 kxploit format
        if (strcmp(dit.d_name, ".") == 0) continue; // ignore "cur dir"
        if (strcmp(dit.d_name, "..") == 0) continue; // ignore "parent dir"
        if (!FIO_SO_ISDIR(dit.d_stat.st_attr)) continue; // ignore files
        if (dit.d_name[0] == '.' && !common::config.show_hidden) continue; // ignore hidden?
        if (strcasecmp(dit.d_name, "NPUZ01234") == 0
            || strcasecmp(dit.d_name, "SCPS10084") == 0
            || strcasecmp(dit.d_name, "ARK_Loader") == 0)
            continue; // ignore ARK launchers
        if (this->isHiddenApp(dit.d_name))
            continue; // hidden through gclite_filter.txt
        
        string fullpath = Eboot::fullEbootPath(path, dit.d_name, common::config.show_dlc);
        if (fullpath.size() == 0){
            if (common::config.scan_cat){
                findEboots((string(path) + dit.d_name + "/").c_str());
            }
            continue;
        }

        Eboot* e = new Eboot(fullpath);
        switch (Eboot::getEbootType(fullpath.c_str())){
        case TYPE_PSN:         this->categories[GAME]->addEntry(e);        break;
        case TYPE_POPS:        this->categories[POPS]->addEntry(e);        break;
        default:               this->categories[HOMEBREW]->addEntry(e);    break;
        }
    }
    sceIoDclose(dir);
}

void GameManager::findISOs(const char* path, Menu* dest){

    if (dest == NULL)
        dest = this->categories[GAME]; // default: the "all games" menu

    SceIoDirent dit; memset(&dit, 0, sizeof(dit));
    int dir = sceIoDopen(path);

    if (dir < 0)
        return;
        
    while (sceIoDread(dir, &dit) > 0){

        if (strcmp(dit.d_name, ".") == 0) continue;
        if (strcmp(dit.d_name, "..") == 0) continue;
        if (dit.d_name[0] == '.' && !common::config.show_hidden) continue;

        string fullpath = string(path)+string(dit.d_name);

        if (FIO_SO_ISDIR(dit.d_stat.st_attr)){
            if (common::config.scan_cat && string(dit.d_name) != string("VIDEO")
                && !this->isHiddenCategory(dit.d_name)){
                findISOs((string(path) + dit.d_name + "/").c_str(), dest);
            }
            continue;
        }
        if (Iso::isISO(fullpath.c_str())) dest->addEntry(new Iso(fullpath));
    }
    sceIoDclose(dir);
}

Menu* GameManager::currentMenu(){
    if (selectedCategory < 0)
        return NULL;
    Menu* menu = this->colMenus[selectedCategory];
    return (menu != NULL) ? menu : this->categories[selectedCategory];
}

Entry* GameManager::getEntry(){
    if (selectedCategory < 0)
        return NULL;
    Menu* menu = currentMenu();
    if (menu == NULL || menu->empty())
        return NULL;
    return menu->getEntry();
}

int GameManager::getPreviousCategory(int current){
    int guard = 0;
    do { 
        if (current > 0){
            current--;
        }
        else{
            current = MAX_CATEGORIES-1;
        }
    } while ((this->colMenus[current] == NULL || this->colMenus[current]->empty()) && ++guard < MAX_CATEGORIES);
    return current;
}

int GameManager::getNextCategory(int current){
    int guard = 0;
    do {
        if (current < MAX_CATEGORIES-1){
            current++;
        }
        else{
            current = 0;
        }
    } while ((this->colMenus[current] == NULL || this->colMenus[current]->empty()) && ++guard < MAX_CATEGORIES);
    return current;
}

void GameManager::moveLeft(){
    if (selectedCategory < 0)
        return;

    SystemMgr::pauseDraw();
    int auxCategory = getPreviousCategory(selectedCategory);
    if (auxCategory != selectedCategory){
        this->categories[selectedCategory]->animStart(-2);
        this->categories[auxCategory]->animStart(-2);
        selectedCategory = auxCategory;
        SystemMgr::resumeDraw();
        common::sound_mp3->play();
    }
    else SystemMgr::resumeDraw();
    sceKernelDelayThread(100000);
}

void GameManager::moveRight(){
    if (selectedCategory < 0)
        return;

    SystemMgr::pauseDraw();
    int auxCategory = getNextCategory(selectedCategory);
    
    if (auxCategory != selectedCategory){
        this->categories[selectedCategory]->animStart(2);
        this->categories[auxCategory]->animStart(2);
        selectedCategory = auxCategory;
        SystemMgr::resumeDraw();
        common::sound_mp3->play();
    }
    else SystemMgr::resumeDraw();
    sceKernelDelayThread(100000);
}

void GameManager::moveUp(){
    if (selectedCategory < 0)
        return;
    scroll.tmp = scroll.x; // reset text scroll
    SystemMgr::pauseDraw();
    Menu* menu = currentMenu();
    if (menu) menu->moveUp();
    SystemMgr::resumeDraw();
}

void GameManager::moveDown(){
    if (selectedCategory < 0)
        return;
    scroll.tmp = scroll.x; // reset text scroll
    SystemMgr::pauseDraw();
    Menu* menu = currentMenu();
    if (menu) menu->moveDown();
    SystemMgr::resumeDraw();
}

void GameManager::stopFastScroll(){
    if (selectedCategory < 0)
        return;
    SystemMgr::pauseDraw();
    Menu* menu = currentMenu();
    if (menu) menu->stopFastScroll();
    SystemMgr::resumeDraw();
}

string GameManager::getFooter(){
    return "";
}

string GameManager::getInfo(){
    if (selectedCategory >= 0){
        Entry* e = getEntry();
        if (e == NULL)
            return "";
        if (common::config.show_path){
            return e->getName() + " <" + e->path + ">";
        }
        return e->getName();
    }
    else if (selectedCategory == -1) return "Loading games...";
    else if (selectedCategory == -2) return "No games available";
    return "Unknown Menu State";
}

void GameManager::draw(){
    if (this->selectedCategory >= 0){
        for (int i=0; i<this->maxDraw; i++){
            if (i == (int)this->selectedCategory)
                continue;
            Menu* col = (this->colMenus[i] != NULL) ? this->colMenus[i] : this->categories[i];
            col->setSlot(i);
            col->draw(false);
            sceKernelDelayThread(0);
        }
        Menu* menu = currentMenu();
        if (this->selectedCategory < this->maxDraw && menu){
            menu->setSlot(this->selectedCategory);
            menu->draw(true);
        }
        if (optionsmenu) optionsmenu->draw();
    }
    if (loadingData || selectedCategory == -1){
        static float angle = 1.0;
        Image* img = common::getImage(IMAGE_WAITICON);
        img->draw_rotate(
            (480-img->texture->width)/2,
            (272-img->texture->height)/2,
            angle
        );
        angle+=0.2;
    }
}

void GameManager::endAllThreads(){
    if (dynamicIconRunning == ICONS_PAUSED){
        dynamicIconRunning = ICONS_STOPPED;
        sceKernelSignalSema(iconSema, 1);
    }
    else{
        sceKernelWaitSema(iconSema, 1, NULL);
        dynamicIconRunning = ICONS_STOPPED;
        sceKernelSignalSema(iconSema, 1);
    }
    sceKernelWaitThreadEnd(iconThread, 0);
}


void GameManager::control(Controller* pad){
    this->applyWorkerUpdates();

    Menu* menu = currentMenu();
    bool busy = (menu == NULL) || menu->isAnimating();
    if (this->input_lock > 0){
        this->input_lock--;
        busy = true; // swallow whatever button is still held right after a category switch
    }

    if (pad->down()){
        this->moveDown();
    }
    else if (pad->up()){
        this->moveUp();
    }
    else {
        this->stopFastScroll();
    }
    
    if (pad->left())
        this->moveLeft();
    else if (pad->right())
        this->moveRight();
    else if (pad->accept()){
        if (selectedCategory >= 0 && !busy){
            common::sound_mp3->play();
            this->execApp();
        }
    }
    else if (pad->start()){
        if (selectedCategory >= 0 && !busy){
            this->startBoot();
        }
    }
    else if (pad->select()){
        if (selectedCategory != -1){
            common::sound_mp3->play();
            this->waitIconsLoad();
            GameManager::updateGameList(NULL);
            this->waitIconsLoad();
        }
    }
    else if (pad->LT()){
        if (selectedCategory >= 0 && !busy){
            common::sound_mp3->play();
            this->gameOptionsMenu();
            pad->flush(); // don't let the menu's exit press leak into the game list
        }
    }
    else if (pad->RT()){
        // R steps the focused column through the available sources
        if (selectedCategory >= 0 && !busy && folderNames.size() > 1){
            this->cycleColumn(1);
        }
    }
    else if (pad->square()){
        // square opens the source list of the focused column, preselected on the current source
        if (selectedCategory >= 0 && !busy && folderNames.size() > 1){
            common::sound_mp3->play();
            this->categoryPicker();
            pad->flush();
        }
    }
}

void GameManager::updateGameList(const char* path){
    if (path == NULL 
        || strncmp(path, "ms0:/PSP/GAME/", 14) == 0 || strncmp(path, "ms0:/ISO/", 9) == 0
        || strncmp(path, "ef0:/PSP/GAME/", 14) == 0 || strncmp(path, "ef0:/ISO/", 9) == 0
        || strncmp(path, "ms0:/PSP/VHBL/", 14) == 0 || strncmp(path, "ms0:/PSP/APPS/", 14) == 0
        || strncmp(path, "ef0:/PSP/VHBL/", 14) == 0 || strncmp(path, "ef0:/PSP/APPS/", 14) == 0
        || strncmp(path, "ms0:/PSP/GAME150/", 17) == 0
      ){
        int icon_status = self->dynamicIconRunning;
        if (icon_status == ICONS_LOADING){
            self->pauseIcons();
        }
        SystemMgr::pauseDraw();
        self->selectedCategory = -1;
        self->scanning = true;
        self->selectionPending = false;
        self->umdChangePending = false;
        for (int i=0; i<MAX_CATEGORIES; i++){
            self->categories[i]->clearEntries();
        }
        SystemMgr::resumeDraw();
        if (icon_status == ICONS_LOADING){
            self->resumeIcons();
        }
    }
}

void GameManager::execApp(){
    if (common::config.fast_gameboot){
        this->endAllThreads();
        this->getEntry()->execute();
    }

    SystemMgr::pauseDraw();
    loadingData = true;
    SystemMgr::resumeDraw();
    this->waitIconsLoad();
    this->getEntry()->loadPics();
    SystemMgr::pauseDraw();
    loadingData = false;
    SystemMgr::resumeDraw();
    if (this->getEntry()->pmfPrompt()){
        this->endAllThreads();
        this->getEntry()->execute();
    }
    this->getEntry()->freeTempData();
    sceKernelDelayThread(0);
}

void GameManager::gameOptionsMenu(){
    if (this->getEntry() == NULL)
        return;
    string fav_label = this->getEntry()->favorite ? TR("Remove from Favorites") : TR("Add to Favorites");
    t_options_entry options_entries[] = {
        {OPTIONS_CANCELLED, "Cancel"},
        {0, (char*)fav_label.c_str()},
        {1, "View Info"},
        {2, "Rename"},
        {3, "Delete"},
    };
    optionsmenu = new OptionsMenu("Game Options", sizeof(options_entries)/sizeof(t_options_entry), options_entries);
    int ret = optionsmenu->control();
    OptionsMenu* aux = optionsmenu;
    optionsmenu = NULL;
    delete aux;

    switch (ret){

    case 0:{
        // add/remove the game from the favorites list (persisted in SEPLUGINS/arkmenu_fav.txt)
        Entry* e = this->getEntry();
        e->favorite = !e->favorite;
        bool found = false;
        for (unsigned int i=0; i<favorites.size(); i++){
            if (favorites[i] == e->path){
                if (!e->favorite){
                    favorites.erase(favorites.begin()+i);
                }
                found = true;
                break;
            }
        }
        if (e->favorite && !found)
            favorites.push_back(e->path);
        this->saveFavorites();
        this->rebuildFavoritesMenu();
    } break;

    case 1:{
        // create a new options menu but each entry is some info about the game
        Entry* e = this->getEntry();
        string path = e->path;
        SfoInfo info = e->getSfoInfo();
        string game_info[] = {
            info.title,
            info.gameid,
            path,
            common::beautifySize(common::fileSize(path))
        };
        string fullname = TR("Name") + " - " + game_info[0];
        string gameid = TR("Game ID") + " - " + game_info[1];
        string fullpath = TR("Path") + " - " + game_info[2];
        string size = TR("Size") + " - " + game_info[3];
        t_options_entry gameinfo_entries[] = {
            {-1, "Cancel"},
            {0, (char*)fullname.c_str()},
            {1, (char*)gameid.c_str()},
            {3, (char*)size.c_str()},
            {2, (char*)fullpath.c_str()},
        };
        optionsmenu = new OptionsMenu("Game Info", sizeof(gameinfo_entries)/sizeof(t_options_entry), gameinfo_entries);
        int res = optionsmenu->control();
        OptionsMenu* aux = optionsmenu;
        optionsmenu = NULL;
        delete aux;
        if (res >= 0){
            TextEditor::clipboard = game_info[res];
        }
    } break;

    case 2:{
        // rename the ISO or Eboot folder name
        int icon_status = this->dynamicIconRunning;
        if (icon_status == ICONS_LOADING)
            this->pauseIcons();
        SystemMgr::pauseDraw();
        Entry* e = this->getEntry();
        string name = e->getName();
        OSK osk;

        char parent[128];
        strcpy(parent, e->path.c_str());
        char* pname = strstr(parent, name.c_str());
        *pname = 0;
        
        osk.init("New name for Game", name.c_str(), 50);
        osk.loop();
        if(osk.getResult() != OSK_CANCEL)
        {
            char tmpText[51];
            osk.getText((char*)tmpText);
            string newpath = (string(parent)+string(tmpText));
            string oldpath = (string(parent)+name);
            
            sceIoRename(oldpath.c_str(), newpath.c_str());
            e->name = tmpText;
            e->path = newpath;
        }
        osk.end();
        SystemMgr::resumeDraw();
        if (icon_status == ICONS_LOADING)
            this->resumeIcons();
    } break;

    case 3:{
        // remove current entry from list (adjusting index and selectedCategory accordingly), delete file or folder depending on ISO/EBOOT
        // make checks to prevent deleting stuff like "UMD Drive" and "Recovery" entries
        t_options_entry opts[] = {
            {OPTIONS_CANCELLED, "Cancel"},
            {0, "Confirm"},
        };
        optionsmenu = new OptionsMenu("Confirm Deletion?", sizeof(opts)/sizeof(t_options_entry), opts);
        int pret = optionsmenu->control();
        OptionsMenu* aux = optionsmenu;
        optionsmenu = NULL;
        delete aux;

        if (pret == OPTIONS_CANCELLED) return;

        Entry* e = this->getEntry();
        string name = e->getName();
        if (name != "UMD Drive" && name != "Recovery Menu"){
            int icon_status = this->dynamicIconRunning;
            if (icon_status == ICONS_LOADING)
                this->pauseIcons();
            // pause drawing so we don't crash due to race condition
            SystemMgr::pauseDraw();
            // get current menu and it's game list
            Menu* category = categories[selectedCategory];
            std::vector<Entry*>* entries = category->entries;
            // remove entry from list
            entries->erase(entries->begin()+category->index);
            // adjust selectedCategory
            int retries = 0;
            while (categories[selectedCategory]->entries->size() == 0){
                if (selectedCategory < POPS) selectedCategory++;
                else selectedCategory = GAME;
                if (++retries >= 4){ // can't find a valid menu
                    selectedCategory = -2; // no games available
                    break;
                }
            }
            // resume drawing
            SystemMgr::resumeDraw();

            // if the focused column's source became empty (its last game was deleted),
            // fall back to whatever that column shows by default
            if (selectedCategory >= 0 && colSource[selectedCategory].size() > 0 &&
                colMenus[selectedCategory] != NULL && colMenus[selectedCategory]->empty()){
                colSource[selectedCategory] = "";
                this->resolveColumns();
            }

            // delete file/folder
            if (e->getType() == string("ISO")){
                sceIoRemove(e->path.c_str());
            }
            else if (e->getType() == string("EBOOT")){
                string path = e->path;
                if (strstr(path.c_str(), "/PSP/SAVEDATA/") != NULL){
                    // remove eboot only
                    sceIoRemove(e->path.c_str());
                }
                else{
                    string path = e->path;
                    // remove entire folder
                    string folder = path.substr(0, path.rfind("/")+1);
                    Browser::recursiveFolderDelete(folder);
                    if(strstr(path.c_str(), "%/") != NULL) {
                        // remove 1.50 kxploit folder
                        path.erase(path.find("%/"));
                        path += '/';
                        folder = path;
                        Browser::recursiveFolderDelete(folder);
                    }
                }
            }
            // free resources
            delete e;
            if (icon_status == ICONS_LOADING)
                this->resumeIcons();
        }
    }break;

    default: break;
    }
}

void GameManager::startBoot(){
    switch (common::config.startbtn){
    case 0: { // Default Start Button (Current)
        this->endAllThreads();
        this->getEntry()->execute();
    } break;
    case 1: { // Last game
        const char* last_game = common::config.last_game;
        if (last_game[0] == 0) break;
        if (Eboot::isEboot(last_game)){
            this->endAllThreads();
            Eboot* eboot = new Eboot(last_game);
            eboot->execute();
        }
        else if (Iso::isISO(last_game)){
            this->endAllThreads();
            Iso* iso = new Iso(last_game);
            iso->execute();
        }
    } break;
    case 2: { // Random ISO
        if (this->categories[GAME]->entries->size() > 0){
            this->endAllThreads();
            int rand_idx = rand() % this->categories[GAME]->entries->size();
            Entry* e = this->categories[GAME]->getEntry(rand_idx);
            e->execute();
        }
    } break;
    default: break;
    }
}

/* =====================================================================
 * ISO folder categories ("folder mode"), gclite_filter.txt sync and favorites
 * ===================================================================== */

#define FAVORITES_FILE "ms0:/SEPLUGINS/arkmenu_fav.txt"
#define COLUMNS_FILE   "ms0:/SEPLUGINS/arkmenu_columns.txt"

/* read SEPLUGINS/gclite_filter.txt so the launcher hides what the XMB hides */
void GameManager::loadGclFilters(){

    this->hiddenCategories.clear();
    this->hiddenApps.clear();

    const char* files[] = {"ms0:/SEPLUGINS/gclite_filter.txt", "ef0:/SEPLUGINS/gclite_filter.txt"};

    for (int f=0; f<2; f++){
        SceUID fp = sceIoOpen(files[f], PSP_O_RDONLY, 0777);
        if (fp < 0)
            continue;

        unsigned int size = (unsigned int)sceIoLseek(fp, 0, PSP_SEEK_END);
        sceIoLseek(fp, 0, PSP_SEEK_SET);

        if (size > 0 && size < 65536){
            char* buf = (char*)malloc(size+1);
            if (buf){
                memset(buf, 0, size+1);
                sceIoRead(fp, buf, size);
                buf[size] = 0;

                int section = 0; // 0 = legacy entries (app folders), 1 = categories, 2 = apps
                char* saveptr = NULL;
                for (char* line = strtok_r(buf, "\r\n", &saveptr); line != NULL; line = strtok_r(NULL, "\r\n", &saveptr)){
                    while (*line == ' ' || *line == '\t') line++;
                    if (line[0] == 0 || line[0] == '#' || line[0] == ';')
                        continue;
                    if (strncasecmp(line, "===HIDDEN CATEGORIES===", 23) == 0){ section = 1; continue; }
                    if (strncasecmp(line, "===HIDDEN APPS===", 17) == 0){ section = 2; continue; }

                    string entry = line;
                    if (strncasecmp(line, "ms0,", 4) == 0) entry = line+4;
                    else if (strncasecmp(line, "ef0,", 4) == 0) entry = line+4;
                    while (entry.size() > 0 && entry[0] == ' ') entry.erase(0, 1);
                    if (entry.size() == 0) continue;
                    // drop a trailing device prefix if present (ms0:/ or ef0:/)
                    if (entry.compare(0, 6, "ms0:/") == 0) entry = entry.substr(6);
                    else if (entry.compare(0, 6, "ef0:/") == 0) entry = entry.substr(6);

                    if (section == 1){
                        this->hiddenCategories.push_back(entry);
                    }
                    else{
                        // legacy format / HIDDEN APPS: match the last path component (the app folder name)
                        unsigned int pos = entry.find_last_of('/');
                        string base = (pos == string::npos) ? entry : entry.substr(pos+1);
                        if (base.size() > 0)
                            this->hiddenApps.push_back(base);
                    }
                }
                free(buf);
            }
        }
        sceIoClose(fp);
    }
}

bool GameManager::isHiddenCategory(const string& name){
    for (unsigned int i=0; i<this->hiddenCategories.size(); i++)
        if (strcasecmp(this->hiddenCategories[i].c_str(), name.c_str()) == 0)
            return true;
    return false;
}

bool GameManager::isHiddenApp(const string& name){
    for (unsigned int i=0; i<this->hiddenApps.size(); i++)
        if (strcasecmp(this->hiddenApps[i].c_str(), name.c_str()) == 0)
            return true;
    return false;
}

/* build one menu per folder in ms0:/ISO/ (folder mode, like Game Categories Lite) */
void GameManager::scanFolderCategories(){

    // drop the old folder menus (folderMenus[0] is categories[GAME], keep it)
    this->favoritesMenu = NULL; // owned by folderMenus, will be rebuilt afterwards
    for (unsigned int i=1; i<folderMenus.size(); i++)
        delete folderMenus[i];
    folderMenus.resize(1);
    folderNames.resize(1);

    if (common::config.scan_cat){
        int ms_is_ef = sctrlKernelMsIsEf();
        const char* bases[] = {"ms0:/ISO/", "ef0:/ISO/"};

        for (int b=0; b<2; b++){
            if (b == 1 && ms_is_ef) break;

            SceIoDirent dit; memset(&dit, 0, sizeof(dit));
            SceUID dir = sceIoDopen(bases[b]);
            if (dir < 0) continue;

            while (sceIoDread(dir, &dit) > 0){
                if (strcmp(dit.d_name, ".") == 0 || strcmp(dit.d_name, "..") == 0) continue;
                if (!FIO_SO_ISDIR(dit.d_stat.st_attr)) continue;
                if (dit.d_name[0] == '.' && !common::config.show_hidden) continue;
                if (string(dit.d_name) == "VIDEO") continue;
                if (this->isHiddenCategory(dit.d_name)) continue;

                Menu* menu = new Menu(GAME);
                this->findISOs((string(bases[b]) + dit.d_name + "/").c_str(), menu);
                if (menu->empty()){
                    delete menu;
                    continue;
                }
                if (common::config.sort_entries)
                    std::sort(menu->entries->begin(), menu->entries->end(), Entry::cmpEntriesForSort);
                folderMenus.push_back(menu);
                folderNames.push_back(dit.d_name);
            }
            sceIoDclose(dir);
        }

        // keep the folders in alphabetical order (00GTA, 01Akcja, ...)
        for (unsigned int i=1; i<folderMenus.size(); i++)
            for (unsigned int j=i+1; j<folderMenus.size(); j++)
                if (strcasecmp(folderNames[i].c_str(), folderNames[j].c_str()) > 0){
                    std::swap(folderMenus[i], folderMenus[j]);
                    std::swap(folderNames[i], folderNames[j]);
                }
    }

    // the columns are re-resolved by the caller (resolveColumns())
}

void GameManager::loadFavorites(){

    this->favorites.clear();

    SceUID fp = sceIoOpen(FAVORITES_FILE, PSP_O_RDONLY, 0777);
    if (fp < 0)
        return;

    unsigned int size = (unsigned int)sceIoLseek(fp, 0, PSP_SEEK_END);
    sceIoLseek(fp, 0, PSP_SEEK_SET);

    if (size > 0 && size < 128*1024){
        char* buf = (char*)malloc(size+1);
        if (buf){
            memset(buf, 0, size+1);
            sceIoRead(fp, buf, size);
            buf[size] = 0;
            char* saveptr = NULL;
            for (char* line = strtok_r(buf, "\r\n", &saveptr); line != NULL; line = strtok_r(NULL, "\r\n", &saveptr)){
                while (*line == ' ' || *line == '\t') line++;
                if (line[0] == 0) continue;
                this->favorites.push_back(line);
            }
            free(buf);
        }
    }
    sceIoClose(fp);
}

void GameManager::saveFavorites(){

    SceUID fp = sceIoOpen(FAVORITES_FILE, PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0777);
    if (fp < 0)
        return;

    string data = "";
    for (unsigned int i=0; i<favorites.size(); i++)
        data += favorites[i] + "\n";
    if (data.size() > 0)
        sceIoWrite(fp, data.c_str(), data.size());
    sceIoClose(fp);
}

void GameManager::applyFavorites(){

    for (unsigned int f=0; f<favorites.size(); f++){
        const string path = favorites[f];
        for (int c=0; c<MAX_CATEGORIES; c++){
            for (unsigned int e=0; e<categories[c]->entries->size(); e++){
                if (categories[c]->entries->at(e)->path == path)
                    categories[c]->entries->at(e)->favorite = true;
            }
        }
        for (unsigned int m=1; m<folderMenus.size(); m++){
            for (unsigned int e=0; e<folderMenus[m]->entries->size(); e++){
                if (folderMenus[m]->entries->at(e)->path == path)
                    folderMenus[m]->entries->at(e)->favorite = true;
            }
        }
    }
}

/* "* Favorites" pseudo category: rebuilt whenever the list changes */
void GameManager::rebuildFavoritesMenu(){

    if (favoritesMenu){
        for (unsigned int i=1; i<folderMenus.size(); i++){
            if (folderMenus[i] == favoritesMenu){
                folderMenus.erase(folderMenus.begin()+i);
                folderNames.erase(folderNames.begin()+i);
                break;
            }
        }
        delete favoritesMenu;
        favoritesMenu = NULL;
    }

    if (favorites.size() == 0){
        this->resolveColumns();
        return;
    }

    Menu* menu = new Menu(GAME);
    for (unsigned int i=0; i<favorites.size(); i++){
        const string path = favorites[i];
        Entry* entry = NULL;
        if (Eboot::isEboot(path.c_str()))
            entry = new Eboot(path);
        else if (Iso::isISO(path.c_str()))
            entry = new Iso(path);
        if (entry){
            entry->favorite = true;
            menu->addEntry(entry);
        }
    }

    if (menu->empty()){
        delete menu;
        this->resolveColumns();
        return;
    }

    if (common::config.sort_entries)
        std::sort(menu->entries->begin(), menu->entries->end(), Entry::cmpEntriesForSort);

    favoritesMenu = menu;
    folderMenus.push_back(menu);
    folderNames.push_back(TR("* Favorites"));
    this->resolveColumns();
}

/* -------------------------------------------------------------------------
 * customizable columns (#86): each of the three columns can show any source
 * (all games, favorites, an ISO folder, homebrew or the PS1 list)
 * ----------------------------------------------------------------------- */

vector<string> GameManager::sourceTokens(){
    vector<string> tokens;
    tokens.push_back("");              // the column's own default menu
    tokens.push_back("favorites");
    for (unsigned int i=1; i<folderNames.size(); i++)
        tokens.push_back(folderNames[i]);
    tokens.push_back("homebrew");
    tokens.push_back("pops");
    return tokens;
}

string GameManager::sourceLabel(const string& token, int col){
    if (token.size() == 0){
        if (col == (int)HOMEBREW) return "Homebrew";
        if (col == (int)POPS) return "PS1";
        return "All games";
    }
    if (token == "favorites") return "* Favorites";
    if (token == "homebrew") return "Homebrew";
    if (token == "pops") return "PS1";
    return token;
}

int GameManager::sourceCount(const string& token, int col){

    Menu* menu = NULL;

    if (token.size() == 0){
        if (col < 0 || col >= MAX_CATEGORIES) return 0;
        menu = this->categories[col];
    }
    else if (token == "favorites"){
        menu = favoritesMenu;
    }
    else if (token == "homebrew"){
        menu = this->categories[HOMEBREW];
    }
    else if (token == "pops"){
        menu = this->categories[POPS];
    }
    else if (token == "games" || token == "all"){
        menu = this->categories[GAME];
    }
    else{
        for (unsigned int f=1; f<folderNames.size(); f++){
            if (folderNames[f] == token){
                menu = folderMenus[f];
                break;
            }
        }
    }

    return (menu != NULL) ? (int)menu->entries->size() : 0;
}

/* map every column source onto an actual menu; a source that vanished falls back to the default */
void GameManager::resolveColumns(){

    for (int i=0; i<MAX_CATEGORIES; i++){

        Menu* menu = this->categories[i];

        if (colSource[i].size() > 0){
            if (colSource[i] == "favorites"){
                if (favoritesMenu) menu = favoritesMenu;
            }
            else if (colSource[i] == "homebrew"){
                menu = this->categories[HOMEBREW];
            }
            else if (colSource[i] == "pops"){
                menu = this->categories[POPS];
            }
            else if (colSource[i] == "games" || colSource[i] == "all"){
                menu = this->categories[GAME];
            }
            else{
                for (unsigned int f=1; f<folderNames.size(); f++){
                    if (folderNames[f] == colSource[i]){
                        menu = folderMenus[f];
                        break;
                    }
                }
            }
        }

        if (menu == NULL)
            menu = this->categories[i];

        this->colMenus[i] = menu;
    }
}

void GameManager::loadColumns(){

    for (int i=0; i<MAX_CATEGORIES; i++)
        colSource[i] = "";

    SceUID fp = sceIoOpen(COLUMNS_FILE, PSP_O_RDONLY, 0777);
    if (fp < 0)
        return;

    char buf[512];
    memset(buf, 0, sizeof(buf));
    int n = sceIoRead(fp, buf, sizeof(buf)-1);
    sceIoClose(fp);
    if (n <= 0)
        return;

    int col = 0;
    char* saveptr = NULL;
    for (char* line = strtok_r(buf, "\r\n", &saveptr); line != NULL && col < MAX_CATEGORIES; line = strtok_r(NULL, "\r\n", &saveptr)){
        string token = line;
        while (token.size() > 0 && (token[0] == ' ' || token[0] == '\t'))
            token.erase(0, 1);
        while (token.size() > 0 && (token[token.size()-1] == ' ' || token[token.size()-1] == '\t'))
            token.erase(token.size()-1);
        if (token == "default" || token == "games" || token == "all")
            token = "";
        colSource[col++] = token;
    }
}

void GameManager::saveColumns(){

    SceUID fp = sceIoOpen(COLUMNS_FILE, PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0777);
    if (fp < 0)
        return;

    string data = "";
    for (int i=0; i<MAX_CATEGORIES; i++){
        data += (colSource[i].size() > 0) ? colSource[i] : string("default");
        data += "\n";
    }
    sceIoWrite(fp, data.c_str(), data.size());
    sceIoClose(fp);
}

/* assign a source to one column and remember it */
void GameManager::assignColumn(int col, const string& token){

    if (col < 0 || col >= MAX_CATEGORIES || colSource[col] == token)
        return;

    SystemMgr::pauseDraw();
    Menu* old_menu = this->colMenus[col];
    colSource[col] = token;
    this->resolveColumns();
    Menu* new_menu = this->colMenus[col];
    if (old_menu != NULL && old_menu != new_menu) old_menu->animStart(2);
    if (new_menu != NULL && new_menu != old_menu) new_menu->animStart(2);
    SystemMgr::resumeDraw();

    common::sound_mp3->play();
    this->saveColumns();
    this->input_lock = 10;
}

/* R trigger: step the focused column through the available sources */
void GameManager::cycleColumn(int direction){

    if (direction == 0)
        return;

    vector<string> tokens = this->sourceTokens();
    int n = (int)tokens.size();
    if (n < 2)
        return;

    int current = 0;
    for (int i=0; i<n; i++){
        if (tokens[i] == colSource[selectedCategory]){
            current = i;
            break;
        }
    }

    this->assignColumn(selectedCategory, tokens[(current + direction + n) % n]);
}

/* square: pick the source of the focused column */
void GameManager::categoryPicker(){

    if (selectedCategory < 0)
        return;

    vector<string> tokens = this->sourceTokens();
    if (tokens.size() < 2)
        return;

    // labels[0] is the cancel row, the rest describe the sources (entry values index into tokens)
    std::vector<string> labels;
    labels.reserve(tokens.size()+1);
    labels.push_back(TR("Cancel"));
    for (unsigned int i=0; i<tokens.size(); i++){
        char count[32];
        snprintf(count, sizeof(count), " (%d)", this->sourceCount(tokens[i], selectedCategory));
        string label = sourceLabel(tokens[i], selectedCategory);
        if (tokens[i].size() == 0)
            label += TR(" (default)");
        labels.push_back(label + string(count));
    }

    t_options_entry* opts = (t_options_entry*)malloc(sizeof(t_options_entry)*labels.size());
    if (opts == NULL)
        return;
    opts[0].value = OPTIONS_CANCELLED;
    opts[0].name = (char*)labels[0].c_str();
    for (unsigned int i=1; i<labels.size(); i++){
        opts[i].value = (int)i - 1;
        opts[i].name = (char*)labels[i].c_str();
    }

    int start = 1;
    for (unsigned int i=0; i<tokens.size(); i++){
        if (tokens[i] == colSource[selectedCategory]){
            start = (int)i + 1;
            break;
        }
    }

    string title = TR("Column");
    char cnum[16];
    snprintf(cnum, sizeof(cnum), " %d", selectedCategory+1);
    title += cnum;

    OptionsMenu* om = new OptionsMenu((char*)title.c_str(), (int)labels.size(), opts, start);
    optionsmenu = om;
    int ret = om->control();
    optionsmenu = NULL;
    delete om;
    free(opts);

    if (ret < 0 || ret >= (int)tokens.size())
        return;

    this->assignColumn(selectedCategory, tokens[ret]);
}
