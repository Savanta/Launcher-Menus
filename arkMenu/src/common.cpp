#include <ctime>
#include <sstream>
#include <dirent.h>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <pspiofilemgr.h>

#include <systemctrl.h>
#include <kubridge.h>

#include "common.h"
#include "controller.h"
#include "animations.h"
#include "system_mgr.h"
#include "lang.h"
#include "browser.h"
#include "iso.h"
#include "eboot.h"

#define RESOURCES_LOAD_PLACE YA2D_PLACE_RAM

using namespace common;

static Image* images[MAX_IMAGES];
static Image* checkbox[2];
static Image* icons[MAX_FILE_TYPES];
static int currentFont = 0;
static int currentLang = 0;
static int currentApp = 0; // Games
static bool flipControl = false;
/* Instance of the animations that are drawn on the menu */
static Anim* cur_anim = NULL; //animations[ANIM_COUNT];

struct tm common::today;
int common::argc;
char** common::argv;
int common::psp_model;
ArkMenuConf common::config;
u32 common::color_text   = GRAY_COLOR;
u32 common::color_dim    = LITEGRAY;
u32 common::color_accent = YELLOW_COLOR;
u32 common::color_marker = YELLOW_COLOR;
ARKConfig common::ark_config = {0};
bool common::is_recovery = false;
string common::theme_path = THEME_NAME;
MP3* common::sound_mp3 = NULL;

char* fonts[] = {
    "FONT.PGF",
    "flash0:/font/ltn0.pgf",
    "flash0:/font/ltn1.pgf",
    "flash0:/font/ltn2.pgf",
    "flash0:/font/ltn3.pgf",
    "flash0:/font/ltn4.pgf",
    "flash0:/font/ltn5.pgf",
    "flash0:/font/ltn6.pgf",
    "flash0:/font/ltn7.pgf",
    "flash0:/font/ltn8.pgf",
    "flash0:/font/ltn9.pgf",
    "flash0:/font/ltn10.pgf",
    "flash0:/font/ltn11.pgf",
    "flash0:/font/ltn12.pgf",
    "flash0:/font/ltn13.pgf",
    "flash0:/font/ltn14.pgf",
    "flash0:/font/ltn15.pgf",
    //"flash0:/font/jpn0.pgf",
    //"flash0:/font/kr0.pgf"
};

extern unsigned char language_selection;
// custom languages
static char* lang_files[] = {
    "en",
    "es",
    "de",
    "fr",
    "pt",
    "it",
    "nl",
    "ru",
    "ukr",
    "ro",
    "lat",
    "jp",
    "ko",
    "cht",
    "chs",
    "pol",
    "latgr",
    "tr",
    //"grk",
    //"thai",
};

// system languages
static char* system_lang[] = {
    "jp",
    "en",
    "fr",
    "es",
    "de",
    "it",
    "nl",
    "pt",
    "ru",
    "ko",
    "cht",
    "chs"
};


static volatile bool do_loading_thread = false;
static volatile SceUID load_thread_id = -1;

void common::dummyMissingHandler(const char* filename){

}

static char* getLangFile(){
    static char file[20];
    char* lang = NULL;
    if (config.syslang){
        int id; sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &id);
        lang = system_lang[id];
    }
    else {
        lang = lang_files[config.language];
    }
    sprintf(file, "lang_%s.json", lang);
    return file;
}

void loadConfig(){
    SceUID fp = sceIoOpen(MENU_SETTINGS, PSP_O_RDONLY, 0777);
    if (fp < 0){
        resetConf();
        return;
    }
    memset(&config, 0, sizeof(ArkMenuConf));
    config.fast_scroll_speed = 2;
    sceIoLseek(fp, 0, PSP_SEEK_SET);
    sceIoRead(fp, &config, sizeof(ArkMenuConf));
    if (config.fast_scroll_speed >= 5)
        config.fast_scroll_speed = 2;
    sceIoClose(fp);

    if (config.syslang){
        language_selection = 0;
    }
    else {
        language_selection = config.language+1;
    }

    if (common::today.tm_mday == 1 && common::today.tm_mon == 3)
        config.language = 10;
}

static void loadFont(){
    unsigned offset=0, size=0;
    if (config.font == 0){
        offset = findPkgOffset(fonts[0], &size, "LANG.ARK", &dummyMissingHandler);
        if (offset && size){
            fonts[0] = "LANG.ARK"; // found font in lang package
        }
        else if ((offset = findPkgOffset(fonts[0], &size, "THEME.ARK", &dummyMissingHandler)) != 0 ){
            fonts[0] = "THEME.ARK"; // found font in theme package
        }
        else if (!fileExists(fonts[0])){
            if (altFont){
                intraFont* aux = font;
                if (aux) intraFontUnload(aux);
                font = altFont;
                config.font = altFontId;
                return;
            }
            else{
                config.font = 1;
            }
        }
    }

    // offload current font
    intraFont* aux = font;
    if (aux) intraFontUnload(aux);
    // load new font
    if (config.font == 0 && !altFont) altFont = intraFontLoadEx(fonts[1], INTRAFONT_CACHE_ALL, 0, 0);
    font = intraFontLoadEx(fonts[config.font], INTRAFONT_CACHE_ALL, offset, size);
    intraFontSetEncoding(font, INTRAFONT_STRING_UTF8);
    // set alt font
    if (altFont) intraFontSetAltFont(font, altFont);
    currentFont = config.font;
}

void common::saveConf(){

    SystemMgr::pauseDraw();

    // reload language
    if (currentLang != language_selection){
        if (language_selection == 0) {
            config.syslang = 1;
            config.language = 0;
        }
        else {
            config.syslang = 0;
            config.language = language_selection-1;
        }
        if (!Translations::loadLanguage(getLangFile()) && language_selection > 1){
            config.syslang = 1;
        }
        currentLang = language_selection;
    }

    // reload font
    if (currentFont != config.font || font == NULL){
        loadFont();
    }

    // swap apps
    if (!is_recovery && currentApp != config.main_menu){
        SystemEntry* ent0 = SystemMgr::getSystemEntry(0);
        SystemEntry* ent1 = SystemMgr::getSystemEntry(1);
        SystemMgr::setSystemEntry(ent1, 0);
        SystemMgr::setSystemEntry(ent0, 1);
        currentApp = config.main_menu;
    }

    SystemMgr::resumeDraw();

    strcpy(config.browser_dir, Browser::getInstance()->getCWD());

    loadAnim();
    
    SceUID fp = sceIoOpen(MENU_SETTINGS, PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC, 0777);
    sceIoWrite(fp, &config, sizeof(ArkMenuConf));
    sceIoClose(fp);
}

void common::resetConf(){
    memset(&config, 0, sizeof(config));
    config.syslang = 1;
    config.font = 1;
    config.sort_entries = 1;
    config.text_glow = 3;
    config.screensaver = 2;
    config.redirect_ms0 = 1;
    config.menusize = 2;
    config.browser_icon0 = 1;
    config.fast_scroll_speed = 2;
    language_selection = 0;
}

void common::launchRecovery(const char* path){
    string fakent = string(common::ark_config.arkpath) + VBOOT_PBP;
    if (fakent != path && common::fileExists(path)){
        struct SceKernelLoadExecVSHParam param;
        
        memset(&param, 0, sizeof(param));
        
        int runlevel = HOMEBREW_RUNLEVEL;
        
        param.args = strlen(path) + 1;
        param.argp = (char*)path;
        param.key = "game";
        sctrlKernelLoadExecVSHWithApitype(runlevel, path, &param);
    }
    else {
        string recovery_prx = string(common::ark_config.arkpath) + RECOVERY_PRX;
        SceUID modid = kuKernelLoadModule(recovery_prx.c_str(), 0, NULL);
        if (modid >= 0){
            int res = sceKernelStartModule(modid, recovery_prx.size() + 1, (void*)recovery_prx.c_str(), NULL, NULL);
            if (res >= 0){
                while (1){sceKernelDelayThread(1000000);}; // wait for recovery to finish
            }
        }
    }
}

static void missingFileHandler(const char* filename){

    if (load_thread_id >= 0){
        stopLoadingThread();
    }

    if (!font){
        font = intraFontLoad(fonts[1], INTRAFONT_CACHE_ASCII);
        intraFontSetEncoding(font, INTRAFONT_STRING_UTF8);
    }
    
    static char msg[64];
    snprintf(msg, 64, "Error, missing file %s", filename);
    
    Controller pad;
    
    while (true){
    
        common::clearScreen(0);
        common::printText(10, 10, msg, GRAY_COLOR, SIZE_BIG, true);
        common::printText(10, 30, "Press cross or circle to launch recovery", GRAY_COLOR, SIZE_LITTLE, false);
        common::printText(10, 50, "Press triangle to exit", GRAY_COLOR, SIZE_LITTLE, false);
        common::flipScreen();
    
        pad.update();
        if (pad.cross() || pad.circle()){
            string recovery_path = string(ark_config.arkpath) + ARK_RECOVERY;
            common::launchRecovery(recovery_path.c_str());
        }
        else if (pad.triangle())
            sctrlKernelExitVSH(NULL);
    }
}

SceOff common::findPkgOffset(const char* filename, unsigned* size, const char* pkgpath, void (*missinghandler)(const char*)){
    
    if (pkgpath == NULL)
        pkgpath = theme_path.c_str();

    if (missinghandler == NULL)
        missinghandler = &missingFileHandler;

    SceUID pkg = sceIoOpen(pkgpath, PSP_O_RDONLY, 0777);
    if (pkg < 0)
        return 0;
     
    unsigned pkgsize = sceIoLseek(pkg, 0, PSP_SEEK_END);
    unsigned size2 = 0;
     
    sceIoLseek(pkg, 0, PSP_SEEK_SET);

    if (size != NULL)
        *size = 0;

    unsigned offset = 0;
    char name[64];
           
    while (offset != 0xFFFFFFFF){
        sceIoRead(pkg, &offset, 4);
        if (offset == 0xFFFFFFFF){
            sceIoClose(pkg);
            missinghandler(filename);
            return 0;
        }
        unsigned namelength;
        sceIoRead(pkg, &namelength, 4);
        sceIoRead(pkg, name, namelength+1);
                   
        if (!strncmp(name, filename, namelength)){
            sceIoRead(pkg, &size2, 4);
    
            if (size2 == 0xFFFFFFFF)
                size2 = pkgsize;

            if (size != NULL)
                *size = size2 - offset;
     
            sceIoClose(pkg);
            return offset;
        }
    }
    missinghandler(pkgpath);
    return 0;
}

void* common::readFile(const char* filename, unsigned* size){
    SceUID fd = sceIoOpen(filename, PSP_O_RDONLY, 0777);
    if (fd < 0) return NULL;

    *size = sceIoLseek32(fd, 0, PSP_SEEK_END);

    if (*size == 0){
        sceIoClose(fd);
        return NULL;
    }

    sceIoLseek32(fd, 0, PSP_SEEK_SET);

    void* buf = malloc(*size);
    sceIoRead(fd, buf, *size);
    sceIoClose(fd);

    return buf;
}

void* common::readFromPKG(const char* filename, unsigned* size, const char* pkgpath){

    unsigned mySize, offset;
    void* data;
    SceUID fp;
    
    if (size == NULL)
        size = &mySize;

    if (pkgpath == NULL)
        pkgpath = theme_path.c_str();

    offset = findPkgOffset(filename, size, pkgpath, &dummyMissingHandler);
    if (offset == 0) goto err_readFromPKG;
    
    fp = sceIoOpen(pkgpath, PSP_O_RDONLY, 0777);
    if (fp < 0) goto err_readFromPKG;
    
    data = malloc(*size);
    sceIoLseek(fp, offset, PSP_SEEK_SET);
    sceIoRead(fp, data, *size);
    sceIoClose(fp);
    return data;

    err_readFromPKG:
    *size = 0;
    return NULL;
}

bool common::has_suffix(const std::string &str, const std::string &suffix)
{
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

u32 common::getMagic(const char* filename, unsigned int offset){
    SceUID fp = sceIoOpen(filename, PSP_O_RDONLY, 0777);
    if (fp < 0)
        return 0;
    u32 magic;
    sceIoLseek(fp, offset, PSP_SEEK_SET);
    sceIoRead(fp, &magic, 4);
    sceIoClose(fp);
    return magic;
}

static int loading_thread(SceSize argc, void* argp){
    float angle = 1.0;
    while (do_loading_thread){
        common::clearScreen(CLEAR_COLOR);
        images[IMAGE_BG]->draw(0, 0);
        images[IMAGE_WAITICON]->draw_rotate(
            (480 - images[IMAGE_WAITICON]->texture->width)/2,
            (272 - images[IMAGE_WAITICON]->texture->height)/2,
            angle
        );
        angle+=0.2;
        common::flipScreen();
        sceKernelDelayThread(0);
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

void common::startLoadingThread(){
    do_loading_thread = true;
    load_thread_id = sceKernelCreateThread("theme_thread", &loading_thread, 0x10, 0x8000, PSP_THREAD_ATTR_USER|PSP_THREAD_ATTR_VFPU, NULL);
    sceKernelStartThread(load_thread_id, 0, NULL);
}

void common::stopLoadingThread(){
    do_loading_thread = false;
    sceKernelWaitThreadEnd(load_thread_id, NULL);
    load_thread_id = -1;
}

void common::loadThemeColors(){

    color_text   = GRAY_COLOR;
    color_dim    = LITEGRAY;
    color_accent = YELLOW_COLOR;
    color_marker = YELLOW_COLOR;

    if (findPkgOffset("COLORS.TXT", NULL, NULL, dummyMissingHandler) <= 0)
        return;

    unsigned size = 0;
    void* data = readFromPKG("COLORS.TXT", &size, NULL);
    if (data == NULL || size == 0){
        if (data) free(data);
        return;
    }

    char* buf = (char*)data;
    char* saveptr = NULL;
    for (char* line = strtok_r(buf, "\r\n", &saveptr); line != NULL; line = strtok_r(NULL, "\r\n", &saveptr)){

        while (*line == ' ' || *line == '\t') line++;
        if (*line == 0 || *line == '#' || *line == ';')
            continue;

        char* eq = strchr(line, '=');
        if (eq == NULL)
            continue;
        *eq = 0;

        string key = line;
        string val = eq+1;
        while (key.size() > 0 && (key[key.size()-1] == ' ' || key[key.size()-1] == '\t'))
            key.erase(key.size()-1);
        while (val.size() > 0 && (val[0] == ' ' || val[0] == '\t'))
            val.erase(0, 1);
        if (key != "text" && key != "dim" && key != "accent" && key != "marker" && key != "favorite")
            continue;
        if (val.size() < 6)
            continue;

        // RRGGBB -> ARK's ABGR word
        unsigned int v = (unsigned int)strtoul(val.substr(val.size()-6).c_str(), NULL, 16);
        u32 r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
        u32 col = 0xFF000000u | (b << 16) | (g << 8) | r;

        if (key == "text")            color_text = col;
        else if (key == "dim")        color_dim = col;
        else if (key == "accent")     color_accent = col;
        else                          color_marker = col;
    }
    free(data);
}

void common::loadTheme(){

    loadThemeColors();

    SceIoStat stat;
    SceOff pkg_bg_off;
    string custom_path1 = string(ark_config.arkpath) + "BG.JPG";
    string custom_path2 = string(ark_config.arkpath) + "BG.PNG";

    if (sceIoGetstat(custom_path1.c_str(), &stat) >= 0){
        images[IMAGE_BG] = new Image(custom_path1.c_str(), YA2D_PLACE_RAM);
    }
    else if (sceIoGetstat(custom_path2.c_str(), &stat) >= 0){
        images[IMAGE_BG] = new Image(custom_path2.c_str(), YA2D_PLACE_RAM);
    }
    else if ((pkg_bg_off=findPkgOffset("DEFBG.JPG", NULL, NULL, dummyMissingHandler)) > 0){
        unsigned size = 0;
        void* data = readFromPKG("DEFBG.JPG", &size, NULL);
        images[IMAGE_BG] = new Image(data, (unsigned long)size, YA2D_PLACE_RAM);
        free(data);
    }
    else if ((pkg_bg_off=findPkgOffset("DEFBG.PNG", NULL, NULL, dummyMissingHandler)) > 0){
        images[IMAGE_BG] = new Image(theme_path, YA2D_PLACE_RAM, pkg_bg_off);
    }

    images[IMAGE_WAITICON] = new Image(theme_path, RESOURCES_LOAD_PLACE, findPkgOffset("WAIT.PNG"));

    images[0]->swizzle();
    images[1]->swizzle();

    startLoadingThread();

    images[IMAGE_SPRITE] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("SPRITE.PNG"));
    images[IMAGE_NOICON] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("NOICON.PNG"));
    images[IMAGE_GAME] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("GAME.PNG"));
    images[IMAGE_FTP] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("FTP.PNG"));
    images[IMAGE_SETTINGS] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("SETTINGS.PNG"));
    images[IMAGE_BROWSER] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("BROWSER.PNG"));
    images[IMAGE_DIALOG] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("BOX.PNG"));
    images[IMAGE_EXIT] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("EXIT.PNG"));
    images[IMAGE_PLUGINS] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("PLUGINS.PNG"));

    SceOff offset = common::findPkgOffset("CFWSETTINGS.PNG", NULL, NULL, dummyMissingHandler);
    images[IMAGE_CFWSETTINGS] = (offset > 0)? new Image(theme_path, RESOURCES_LOAD_PLACE, offset) : NULL;

    // the header bar: a theme may ship its own HEADER.PNG, otherwise BOX.PNG is stretched
    SceOff header_offset = common::findPkgOffset("HEADER.PNG", NULL, NULL, dummyMissingHandler);
    if (header_offset > 0)
        images[IMAGE_HEADER] = new Image(theme_path, RESOURCES_LOAD_PLACE, header_offset);
    else
        images[IMAGE_HEADER] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("BOX.PNG"));

    icons[FOLDER] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("FOLDER.PNG"));
    icons[FILE_BIN] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("FILE.PNG"));
    icons[FILE_TXT] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("TXT.PNG"));
    icons[FILE_PBP] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("PBP.PNG"));
    icons[FILE_PRX] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("PRX.PNG"));    
    icons[FILE_ISO] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("ISO.PNG"));
    icons[FILE_ZIP] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("ZIP.PNG"));
    icons[FILE_MUSIC] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("MUSIC.PNG"));
    icons[FILE_PICTURE] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("PICTURE.PNG"));

    checkbox[1] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("CHECK.PNG"));
    checkbox[0] = new Image(theme_path, RESOURCES_LOAD_PLACE, common::findPkgOffset("UNCHECK.PNG"));
    
    for (int i=2; i<MAX_IMAGES; i++){
        if (images[i]){
            images[i]->swizzle();
            images[i]->is_system_image = true;
        }
    }
    
    unsigned mp3_size;
    void* mp3_buffer = readFromPKG("SOUND.MP3", &mp3_size);
    sound_mp3 = new MP3(mp3_buffer, mp3_size);

}

void common::loadAnim(){
    if (cur_anim && cur_anim->getId() == config.animation) return; // already loaded
    Anim* aux = cur_anim;
    switch (config.animation){
    case ANIM_PIXEL: cur_anim = new PixelAnim(); break;
    case ANIM_WAVES: cur_anim = new Waves(); break;
    case ANIM_SPRITES: cur_anim = new Sprites(); break;
    case ANIM_FIRE: cur_anim = new Fire(); break;
    case ANIM_TETRIS: cur_anim = new Tetris(); break;
    case ANIM_MATRIX: cur_anim = new Matrix(); break;
    case ANIM_HACKER: cur_anim = new Hacker(); break;
    case ANIM_BSOD: cur_anim = new BSoD(); break;
    case ANIM_SNOW: cur_anim = new SnowAnim(); break;
    case ANIM_GOL: cur_anim = new GoLAnim(); break;
    case ANIM_NO: cur_anim = new NoAnim(); break;
    }
    if (aux) delete aux;
}

void common::loadData(int ac, char** av, int recovery){

    argc = ac;
    argv = av;

    time_t now = sceKernelLibcTime(NULL);
    today = *localtime(&now);

    psp_model = kuKernelGetModel();

    sctrlArkGetConfig(&ark_config);

    sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);
    sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    
    loadConfig();

    loadAnim();

    // check to run last game
    const char* last_game = common::config.last_game;
    if (last_game[0] != 0) {
        Controller pad;
        pad.update(1);
        if (!recovery && (pad.LT() || config.app_autoboot)){
            common::stopLoadingThread();
            if (Eboot::isEboot(last_game)){
                Eboot* eboot = new Eboot(last_game);
                eboot->execute(true);
            }
            else if (Iso::isISO(last_game)){
                Iso* iso = new Iso(last_game);
                iso->execute(true);
            }
        }
    }

    loadTheme();

    currentFont = config.font;
    currentLang = language_selection;
    currentApp = config.main_menu;

    Translations::loadLanguage(getLangFile());
    
    loadFont();

    if (currentFont != config.font){
        currentFont = config.font;
    }
    
}

void common::deleteTheme(){
    for (int i=0; i<MAX_IMAGES; i++){
        if (images[i]){
            delete images[i];
            images[i] = NULL;
        }
    }
    for (int i=0; i<MAX_FILE_TYPES; i++){
        if (icons[i]) {
            delete icons[i];
            icons[i] = NULL;
        }
    }
    delete checkbox[0];
    delete checkbox[1];
    delete sound_mp3;
}

void common::deleteData(){
    deleteTheme();
    intraFontUnload(font);
}

void common::setThemePath(char* path){
    if (path == NULL) theme_path = THEME_NAME;
    else theme_path = path;
}

bool common::isFolder(SceIoDirent* dit){
    return FIO_SO_ISDIR(dit->d_stat.st_attr) || FIO_S_ISDIR(dit->d_stat.st_mode);
}

bool common::fileExists(const std::string &path){
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISREG(sb.st_mode));
}

bool common::folderExists(const std::string &path){
    struct stat sb;
    return (stat(path.c_str(), &sb) == 0 && S_ISDIR(sb.st_mode));
}

long common::fileSize(const std::string &path){
    struct stat stat_buf;
    int rc = stat(path.c_str(), &stat_buf);
    return rc == 0 ? stat_buf.st_size : 0;
}

u64 common::deviceSize(const std::string path){
    struct DeviceSize {
        u32 maxClusters;
        u32 freeClusters;
        u32 maxSectors;
        u32 sectorSize;
        u32 sectorCount;
    } devsize;
    void* command = (void*)&devsize;
    memset(&devsize, 0, sizeof(devsize));
    string devpath = path.substr(0, path.find(":")+1);
    sceIoDevctl(devpath.c_str(), 0x02425818, &command, sizeof(command), NULL, 0);
    return (u64)devsize.freeClusters*(u64)devsize.sectorSize*(u64)devsize.sectorCount;
}

string common::beautifySize(u64 size){
    ostringstream txt;

    if (size < 1024)
        txt<<size<<" B";
    else if (1024 < size && size < 1048576)
        txt<<round(float(size*100)/1024.f)/100.f<<" KB";
    else if (1048576 < size && size < 1073741824)
        txt<<round(float(size*100)/1048576.f)/100.f<<" MB";
    else
        txt<<round(float(size*100)/1073741824.f)/100.f<<" GB";
    return txt.str();
}

Image* common::getImage(int which){
    return (which < MAX_IMAGES)? images[which] : images[IMAGE_NOICON];
}

Image* common::getIcon(int which){
    return (which < MAX_FILE_TYPES)? icons[which] : images[IMAGE_NOICON];
}

Image* common::getCheckbox(int which){
    return checkbox[which&1];
}

bool common::isSharedImage(Image* img){

    if (img->is_system_image)
        return true;

    for (int i=0; i<MAX_IMAGES; i++){
        if (images[i] == img)
            return true;
    }
    return false;
}

void common::printText(float x, float y, const char* text, u32 color, float size, int glow, TextScroll* scroll, int translate){

    if (font == NULL)
        return;

    // the three "generic" colours follow the active theme (see COLORS.TXT)
    if (color == GRAY_COLOR)
        color = common::color_text;
    else if (color == LITEGRAY)
        color = common::color_dim;
    else if (color == YELLOW_COLOR)
        color = common::color_accent;

    string translated = (translate)? TR(text) : text;
    intraFont* textFont = font;

    if (translated != text){
        size *= text_size;
    }
    
    if (!translate && altFont && !non_latin_filenames){
        textFont = altFont;
    }

    u32 secondColor = BLACK_COLOR;
    u32 arg5 = INTRAFONT_WIDTH_VAR;
    
    if (glow && config.text_glow){
        int val = 0;
        float t = (float)((float)(clock() % CLOCKS_PER_SEC)) / ((float)CLOCKS_PER_SEC);
        if(config.text_glow == 1) {
            val = (t < 0.5f) ? t*311 : (1.0f-t)*311;
        }
        else if(config.text_glow == 2) {
            val = (t < 0.5f) ? t*411 : (1.0f-t)*411;
        }
        else {
            val = (t < 0.5f) ? t*511 : (1.0f-t)*511;
        }
        secondColor = (0xFF<<24)+(val<<16)+(val<<8)+(val);
    }
    if (scroll){
        arg5 = INTRAFONT_SCROLL_LEFT;
    }
    
    intraFontSetStyle(textFont, size, color, secondColor, 0.f, arg5);
    if (altFont) intraFontSetStyle(altFont, size, color, secondColor, 0.f, arg5);

    if (scroll){
        if (x != scroll->x || y != scroll->y){
            scroll->x = x;
            scroll->tmp = x;
            scroll->y = y;
        }
        if (scroll->w <= 0 || scroll->w >= 480) scroll->w = 200;
        scroll->tmp = intraFontPrintColumn(textFont, scroll->tmp, y, scroll->w, translated.c_str());
    }
    else
        intraFontPrint(textFont, x, y, translated.c_str());
    
}

int common::calcTextWidth(const char* text, float size, int translate){
    string translated = (translate)? TR(text) : text;
    intraFont* textFont = font;
    if (translated != text){
        size *= text_size;
    }
    if (!translate && altFont){
        textFont = altFont;
    }
    intraFontSetStyle(textFont, size, 0, 0, 0.f, INTRAFONT_WIDTH_VAR);
    float w = intraFontMeasureText(textFont, translated.c_str());
    return (int)ceil(w);
}

void common::clearScreen(u32 color){
    ya2d_start_drawing();
    ya2d_clear_screen(color);
    flipControl = true;
}

void common::drawBorder(){
    ya2d_draw_rect(10, 20, 80, 254, GRAY_COLOR, 0);
    ya2d_draw_rect(170, 20, 80, 254, GRAY_COLOR, 0);
    ya2d_draw_rect(330, 20, 80, 254, GRAY_COLOR, 0);
}

void common::drawScreen(){
    if (cur_anim->canDrawBackground())
        getImage(IMAGE_BG)->draw(0, 0);

    cur_anim->draw();
}

void common::flipScreen(){
    if (!flipControl)
        return;

    sceDisplayWaitVblankStart();
    ya2d_finish_drawing();
    ya2d_swapbuffers();
    flipControl = false;
};

void common::upperString(char* str){
    while (*str){
        if (*str >= 'a' && *str <= 'z')
            *str -= 0x20;
        str++;
    }
}

int common::maxString(string* strings, int n_strings){
    int max = 0;
    for (int i = 0; i<n_strings; i++){
        int len = strings[i].length();
        if (len > max)
            max = len;
    }
    return max;
}

std::string common::getExtension(std::string path){
    std::string ext = path.substr(path.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}
