/* Category Class that defines each of the individual menus */

#include "menu.h"
#include "mp3.h"
#include "system_mgr.h"

static SceUID iconSema = -1;

static float easeMenuAnimation(float progress){
    return progress * progress * (3.f - 2.f * progress);
}

static float menuRowY(float row){
    if (row < 0)
        return (row <= -1.f) ? 60 + (row + 1) * 40 : 60 + (row + 1) * 45;
    return (row <= 1.f) ? 105 + row * 85 : 190 + (row - 1) * 40;
}

static int selectedMenuRowY(int height){
    return (height == 80) ? 105 : (272 - height) / 2 + 5;
}

static float fastScrollInterpolation(){
    static const float factors[] = {0.15f, 0.22f, 0.3f, 0.45f, 0.65f};
    unsigned char speed = common::config.fast_scroll_speed;
    if (speed >= sizeof(factors) / sizeof(factors[0]))
        speed = 2;
    return factors[speed];
}

Menu::Menu(EntryType t){
    this->type = t;
    this->slot = (int)t;
    this->slideX = 0.f;
    this->index = 0;
    this->threadIndex = 0;
    this->animating = 0;
    this->visualIndex = 0.f;
    this->fastScrolling = false;
    this->animDelay = true;
    this->animState = 0.f;
    this->initLoad = false;
    this->stopLoading = false;
    this->entries = new vector<Entry*>();
    if (iconSema < 0)
        iconSema = sceKernelCreateSema("icon_sema",  0, 1, 1, NULL);
}


Menu::~Menu(){
    this->clearEntries();
    delete this->entries;
}

void Menu::freeIcons(){
    for (int i = 0; i < this->threadIndex-5; i++)
        this->getEntry(i)->freeIcon();

    for (int i = this->threadIndex+6; i<this->entries->size(); i++)
        this->getEntry(i)->freeIcon();
}

bool Menu::checkIconsNeeded(bool isSelected){
    int threshold = (isSelected)? 2 : 5;
    for (int i = max(this->threadIndex-threshold, 0); i < min((int)this->entries->size(), this->threadIndex+threshold+1); i++){
        if (this->getEntry(i)->icon0 == common::getImage(IMAGE_WAITICON))
            return true;
    }
    return false;
}

void Menu::checkIndex(){
    if (this->index>=this->entries->size()){
        this->index = this->entries->size()-1;
    }
    if (this->index<0){
        this->index = 0;
    }
}

void Menu::loadIconsDynamic(bool isSelected){
    SystemMgr::pauseDraw();
    bool skip = this->fastScrolling || this->entries->size() == 0 || stopLoading;
    if (!skip){
        this->checkIndex();
        this->threadIndex = this->index; // prevents our working index from changing
    }
    SystemMgr::resumeDraw();
    if (skip)
        return; // we don't need to load any icons

    sceKernelWaitSema(iconSema, 1, NULL);

    this->stopLoading = false;
    SystemMgr::pauseDraw();
    freeIcons(); // delete any icon that won't be loaded
    SystemMgr::resumeDraw();
    SystemMgr::pauseDraw();
    bool iconsNeeded = checkIconsNeeded(isSelected);
    SystemMgr::resumeDraw();
    if (!iconsNeeded){ // check if we need to load icons
        sceKernelSignalSema(iconSema, 1);
        return;
    }
    // start loading the needed icons
    for (int i = max(0, this->threadIndex-5); i < min((int)this->entries->size(), this->threadIndex+6); i++){
        SystemMgr::pauseDraw();
        bool stop = this->index != this->threadIndex || this->stopLoading;
        bool needsIcon = !stop &&
            this->getEntry(i)->icon0 == common::getImage(IMAGE_WAITICON);
        SystemMgr::resumeDraw();
        if (stop)
            break; // stop loading the icons if the state of the menu has changed
        if (needsIcon){
            Entry* entry = this->getEntry(i);
            if (this->getEntry(i)->icon0 == common::getImage(IMAGE_WAITICON))
                entry->loadIcon();
        }
    }
    initLoad = !this->stopLoading;
    sceKernelSignalSema(iconSema, 1);
}

bool Menu::waitIconsLoad(bool isSelected, bool forceQuit){
    if (this->entries->size() == 0) return true;
    this->stopLoading = forceQuit;
    sceKernelWaitSema(iconSema, 1, NULL); // wait for the thread to release the semaphore
    Entry* entry = this->getEntry();
    if (entry->icon0 == common::getImage(IMAGE_WAITICON))
        entry->loadIcon();
    sceKernelSignalSema(iconSema, 1);
    return true;
}

void Menu::resumeIconLoading(){
    SystemMgr::pauseDraw();
    this->stopLoading = false;
    SystemMgr::resumeDraw();
}

void Menu::draw(bool selected){

    if (this->entries->size() == 0)
        return;

    this->checkIndex();

    int xoffset = this->slot*144 + (this->slot+1)*10 + 5 + (int)this->slideX;
    int yoffset = 20;
    int curentry_yoffset = 0;
    bool scrollAnimation = !fastScrolling && (animating == -1 || animating == 1);
    bool categoryAnimation = animating == 2 || animating == -2;
    float indexDistance = (float)this->index - this->visualIndex;
    bool finishAnimation = (scrollAnimation || categoryAnimation) && animState >= 1.f;
    float progress = animState < 1.f ? animState : 1.f;
    float easedProgress = easeMenuAnimation(progress);

    // draw scrollbar
    if (selected && fastScrolling){
        int height = 240/entries->size();
        int x = xoffset-7;
        int y = yoffset+10;
        ya2d_draw_rect(x+2, y, 3, height*entries->size(), DARKGRAY, 1);
        ya2d_draw_rect(x+1, y + index*height, 5, height, DARKGRAY, 1);
        ya2d_draw_rect(x+3, y, 1, height*entries->size(), LITEGRAY, 1);
        ya2d_draw_rect(x+2, y + index*height, 3, height, LITEGRAY, 1);
    }

    if (categoryAnimation){
        float activeProgress = selected ? easedProgress : 1.f - easedProgress;
        int first = max(0, this->index - 4);
        int last = min((int)this->entries->size(), this->index + 6);

        for (int i = first; i < last; i++){
            float row = i - this->index;
            Image* icon = this->getEntry(i)->icon0;
            if (!icon || !icon->texture)
                continue;

            bool focused = row == 0.f;
            float inactiveY = 100.f + row * 40.f;
            float activeY = focused
                ? selectedMenuRowY(icon->texture->height)
                : menuRowY(row);
            float startY = selected ? inactiveY : activeY;
            float endY = selected ? activeY : inactiveY;
            float y = startY + (endY - startY) * easedProgress;
            float startScale = (focused && !selected) ? 1.f : 0.5f;
            float endScale = (focused && selected) ? 1.f : 0.5f;
            float scale = startScale + (endScale - startScale) * easedProgress;
            float focus = focused ? (selected ? easedProgress : 1.f - easedProgress) : 0.f;
            float x = xoffset + 5.f * focus;
            icon->draw_scale((int)x, (int)y, scale, scale);
        }

        if (finishAnimation){
            animating = 0;
            animState = 0.f;
        }
        else if (!this->animDelay){
            animState += 0.125f;
        }
        return;
    }

    if (selected && (fastScrolling ||
        (animating == 0 && (indexDistance > 0.02f || indexDistance < -0.02f)))){
        if (indexDistance > -0.02f && indexDistance < 0.02f)
            this->visualIndex = (float)this->index;
        else
            this->visualIndex += indexDistance * fastScrollInterpolation();

        int first = max(0, (int)this->visualIndex - 4);
        int last = min((int)this->entries->size(), (int)this->visualIndex + 6);
        for (int layer = 0; layer < 3; layer++){
            for (int i = first; i < last; i++){
                float row = i - this->visualIndex;
                float distance = row < 0.f ? -row : row;
                int drawLayer = distance < 0.5f ? 2 : (distance < 1.5f ? 1 : 0);
                if (drawLayer != layer)
                    continue;

                Image* icon = this->getEntry(i)->icon0;
                if (!icon || !icon->texture)
                    continue;
                float focus = distance < 1.f ? 1.f - distance : 0.f;
                float scale = 0.5f + 0.5f * focus;
                float x = xoffset + 5.f * focus;
                float y = row == 0.f ? selectedMenuRowY(icon->texture->height) : menuRowY(row);
                icon->draw_scale((int)x, (int)y, scale, scale);
            }
        }

        if (indexDistance > -0.02f && indexDistance < 0.02f){
            this->visualIndex = (float)this->index;
            if (!fastScrolling)
                this->animating = 0;
        }
        return;
    }
    
    if (!scrollAnimation && !categoryAnimation && animState >= 1.0f){
        if (animating == -1){
            if (this->index < this->entries->size()-1)
                this->index++;
            else
                animating = 0;
        }
        else if (animating == 1){
            if (this->index > 0)
                this->index--;
            else
                animating = 0;
        }
        if (fastScrolling){
            animState = 0.74f;
            animDelay = false;
        }
        else
            animating = 0;
    }

    if (fastScrolling && (animating == -1 || animating == 1) && !this->animDelay)
        animState += 0.25f;

    if (scrollAnimation && selected){
        int first = max(0, this->index - 4);
        int last = min((int)this->entries->size(), this->index + 6);

        for (int layer = 0; layer < 3; layer++){
            for (int i = first; i < last; i++){
                int startRow = i - this->index;
                int endRow = startRow + animating;
                bool startsSelected = startRow == 0;
                bool endsSelected = endRow == 0;
                int drawLayer = endsSelected ? 2 : (startsSelected ? 1 : 0);
                if (drawLayer != layer)
                    continue;

                Image* icon = this->getEntry(i)->icon0;
                if (!icon || !icon->texture)
                    continue;
                float startScale = startsSelected ? 1.f : 0.5f;
                float endScale = endsSelected ? 1.f : 0.5f;
                float scale = startScale + (endScale - startScale) * easedProgress;
                int startX = xoffset + (startsSelected ? 5 : 0);
                int endX = xoffset + (endsSelected ? 5 : 0);
                int startY = startsSelected
                    ? selectedMenuRowY(icon->texture->height)
                    : menuRowY(startRow);
                int endY = endsSelected
                    ? selectedMenuRowY(icon->texture->height)
                    : menuRowY(endRow);
                float x = startX + (endX - startX) * easedProgress;
                float y = startY + (endY - startY) * easedProgress;
                icon->draw_scale((int)x, (int)y, scale, scale);
            }
        }

        if (finishAnimation){
            this->index -= animating;
            this->checkIndex();
            this->visualIndex = (float)this->index;
            animating = 0;
            animState = 0.f;
        }
        else if (!this->animDelay){
            animState += 0.125f;
        }
        return;
    }

    for (int i=this->index-2; yoffset<258; i++){
        if (i < 0){
            yoffset+=40;
            continue;
        }

        if (i >= this->entries->size()){
            break;
        }
            
        if (selected && i == this->index){
            curentry_yoffset = yoffset;
            yoffset += 90;
            continue;
        }
        else{
            Image* e_icon = getEntry(i)->icon0;
            if (!e_icon || !e_icon->texture){
                yoffset += 40;
                continue;
            }
            e_icon->draw_scale(xoffset, yoffset, 0.5f, 0.5f);
            if (getEntry(i)->favorite)
                common::printText(xoffset+38, yoffset-2, "*", common::color_marker, SIZE_MEDIUM, 1, NULL, 0);
            yoffset += 40;
        }
    }

    if (selected){
        Image* e_icon = getEntry(this->index)->icon0;
        if (!e_icon || !e_icon->texture)
            return;
        int height = e_icon->texture->height;
        if (height != 80)
            curentry_yoffset = (272-height)/2;
        
        int cw = common::calcTextWidth("...", 1.f, 0);
        float factor = float(e_icon->texture->width)/float(cw);
        common::printText(xoffset, curentry_yoffset+(height/2), "...", WHITE_COLOR, factor, 1, NULL, 0);
        e_icon->draw(xoffset+5, curentry_yoffset+5);
        if (getEntry(this->index)->favorite)
            common::printText(xoffset + e_icon->texture->width - 6, curentry_yoffset + 2, "*", common::color_marker, SIZE_BIG, 1, NULL, 0);
    }

    if (scrollAnimation || categoryAnimation){
        if (finishAnimation){
            animating = 0;
            animState = 0.f;
        }
        else if (!this->animDelay){
            animState += 0.125f;
        }
        return;
    }

    // at rest each column shows what it is (category name) and which game is selected
    if (animating == 0 && !fastScrolling && this->entries->size() > 0){

        Entry* cur = getEntry();

        if (!selected && this->label.size() > 0)
            common::printText(xoffset+2, 146, this->label.c_str(), LITEGRAY, SIZE_TINY, 0, NULL, 0);

        int title_y = selected ? (curentry_yoffset + 84) : 160;
        float title_size = selected ? SIZE_LITTLE : SIZE_TINY;
        string title = cur->getName();
        while (title.size() > 4 && common::calcTextWidth(title.c_str(), title_size, 0) > 140)
            title.erase(title.size()-1);

        common::printText(xoffset+2, title_y, title.c_str(), GRAY_COLOR, title_size, 0, NULL, 0);
    }
}

void Menu::animStart(int direction){
    animating = direction;
    animState = 0.f;
    animDelay = false;
}

bool Menu::isAnimating(){
    SystemMgr::pauseDraw();
    bool ret = animating != 0;
    SystemMgr::resumeDraw();
    return ret;
}

bool Menu::empty(){
    bool ret = !this->entries->size();
    return ret;
}

void Menu::addEntry(Entry* e){
    this->entries->push_back(e);
}

Entry* Menu::getEntry(){
    return entries->at(this->index);
}

Entry* Menu::getEntry(int index){
    return entries->at(index);
}

void Menu::clearEntries(){
    for (int i=0; i<entries->size(); i++){
        Entry* entry = entries->at(i);
        entries->at(i) = NULL;
        delete entry;
    }
    this->entries->clear();
    this->index = 0;
    this->visualIndex = 0.f;
    this->initLoad = false;
}

void Menu::moveUp(){
    if (animating == 2 || animating == -2){
        this->visualIndex = (float)this->index;
        animating = 0;
        animState = 0.f;
        fastScrolling = false;
    }

    if (animating || fastScrolling){
        if (!fastScrolling){
            this->visualIndex = (float)this->index;
            if (animating == -1 || animating == 1)
                this->visualIndex -= animating * easeMenuAnimation(this->animState);
            animating = 0;
        }
        fastScrolling = true;
        if (this->index <= 0){
            animating = 0;
            this->index = 0;
            this->stopFastScroll();
        }
        else{
            this->index--;
            common::sound_mp3->play();
        }
    }
    else if (this->index > 0){
        common::sound_mp3->play();
        this->stopFastScroll();
        animStart(1);
    }
    else
        this->stopFastScroll();
}

void Menu::moveDown(){
    if (animating == 2 || animating == -2){
        this->visualIndex = (float)this->index;
        animating = 0;
        animState = 0.f;
        fastScrolling = false;
    }

    if (animating || fastScrolling){
        if (!fastScrolling){
            this->visualIndex = (float)this->index;
            if (animating == -1 || animating == 1)
                this->visualIndex -= animating * easeMenuAnimation(this->animState);
            animating = 0;
        }
        fastScrolling = true;
        if (this->index >= this->entries->size()-1){
            animating = 0;
            this->index = this->entries->size()-1;
            this->stopFastScroll();
        }
        else{
            common::sound_mp3->play();
            this->index++;
        }
    }
    else if (this->index < this->entries->size()-1){
        common::sound_mp3->play();
        this->stopFastScroll();
        animStart(-1);
    }
    else
        this->stopFastScroll();
}

void Menu::stopFastScroll(){
    fastScrolling = false;
    animDelay = false;
}
