#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <curses.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>
#include <minizip/zip.h>

#define ENTER 10
#define TAB '\t'
#define FOLDERCHUNK 16
#define FILECHUNK 64

int n_folders = 0;
int n_files = 0;
int n_entries = 0;

typedef struct ARCHIVE {
    char* tab;
    char* name;
    size_t size;
} ARCHIVE;

typedef struct FOLDER{
    char* name;
    struct FOLDER* parent;
    struct FOLDER* subdir;
    size_t subcount;
    size_t count;
    size_t capacity;
    size_t file_capacity;
    ARCHIVE* content;
} FOLDER;

//Concatenate two strings to acess a path, return to the parent path or get the directory's name
void returnString (char** path, const char* argument) {
    char* scissor = strrchr(*path, '/');
    size_t lenght;

    if (scissor == NULL && (strcmp(argument, "path") == 0 || strcmp(argument, "name") == 0)) {
        scissor = strrchr(*path, '\\');
        if (strcmp(argument, "path") == 0 && scissor != NULL) {
            *scissor = '\0';
        } else if (strcmp(argument, "name") == 0 && scissor != NULL) {
            lenght = strlen(scissor)-1;
            memmove(*path, scissor+1, lenght*sizeof(char));
            path[0][lenght] = '\0';
        }
    } else if (scissor != NULL && (strcmp(argument, "path") == 0 || strcmp(argument, "name") == 0)) {
        if (path[0][strlen(path[0])-1] == '/') {
            *scissor = '\0';
            scissor = strrchr(*path, '/');
        }
        if (strcmp(argument, "name") == 0 && scissor != NULL) {
            lenght = strlen(scissor);
            memmove(*path, scissor+1, lenght*sizeof(char));
            path[0][lenght] = '\0';
        } else if (strcmp(argument, "path") == 0 && scissor != NULL) {
            scissor[1] = '\0';
        }
    } else {
        lenght = strlen(*path)+strlen(argument)+2;
        char* backup = strdup(*path);
        char* temp = (char*)realloc(*path, lenght*sizeof(char));
        if (temp == NULL) {
            perror("Could not reallocate memory for new path");

            if (backup != NULL) {
                *path = (char*)calloc(lenght, sizeof(char));
                if (scissor != NULL) {
                    snprintf(*path, lenght, "%s/%s", backup, argument);
                } else {
                    snprintf(*path, lenght, "%s\\%s", backup, argument);
                }
            }
        } else {
            free(backup);
            *path = temp;
            if (scissor != NULL) {
                strcat(*path, "/");
            } else {
                strcat(*path, "\\");
            }
            strcat(*path, argument);
        }
    }
}

void freeFolder(FOLDER* folder) {
    for (;folder->count > 0; folder->count--) {
        ARCHIVE* file = &folder->content[folder->count-1];
        free(file->name);
        free(file->tab);
    }
    for (;folder->subcount > 0; folder->subcount--) {
        freeFolder(&folder->subdir[folder->subcount-1]);
    }
    free(folder->subdir);
    free(folder->name);
}

//Check for the filetype, counting zip file
int fileType(const char* path) {
    char* pointer = strrchr(path, '/');
    if (pointer == NULL) {
        struct stat fileStat;

        if (stat(path, &fileStat) == 0) {
            if (S_ISDIR(fileStat.st_mode)) {
                return 0;
            } else if (S_ISREG(fileStat.st_mode)) {
                if (strstr(path, ".zip") != NULL) {
                    return 2;
                }
                return 1;
            } else {
                printf("%s isn't a directory or file\n", path);
                return -1;
            }
        } else {
            return 1;
        }
    } else if (pointer != NULL){
        if (path[strlen(path)-1] == '/') {
            return 0;
        } else {
            return 1;
        }
    } else {
        perror("");
        return -1;
    }
    return -1;
}

FOLDER* createFolder(FOLDER* parent, const char* name) {
    FOLDER* folder = (FOLDER*)malloc(sizeof(FOLDER));
    if (folder == NULL) {
        perror("Error creating new folder in memory");
        return NULL;
    }
    folder->name = strdup(name);
    folder->content = NULL;
    folder->subdir = NULL;
    folder->count = folder->subcount = 0;
    folder->capacity = FOLDERCHUNK;
    folder->file_capacity = FILECHUNK;
    folder->parent = parent;
    
    return folder;
}

void addFolder(FOLDER** parent, FOLDER* subdir) {
    if (parent[0]->subcount == 0) {
        parent[0]->subdir = (FOLDER*)calloc(parent[0]->capacity, sizeof(FOLDER));

        if (parent[0]->subdir == NULL) {
            perror("Error allocating memory for new folder");
        }
    } else if (parent[0]->subcount >= parent[0]->capacity) {
        parent[0]->capacity *= 2;
        printf("Reallocating more memory: %d\n", (int)parent[0]->capacity);
        parent[0]->subdir = (FOLDER*)realloc(parent[0]->subdir, (int)parent[0]->capacity*sizeof(FOLDER));

        if (parent[0]->subdir == NULL) {
            perror("Error reallocating memory for new folder");
        }
    }

    parent[0]->subdir[parent[0]->subcount] = *subdir;
    parent[0]->subcount++;
}

//Read from zip file to get file content
ARCHIVE* getUnzip(unzFile* file, const char* name) {
    char *buffer, *placeholder;
    size_t bytesRead, length = 0, capacity = 1024;
    ARCHIVE* model = (ARCHIVE*)malloc(sizeof(ARCHIVE));
    buffer = (char*)calloc(1025, sizeof(char));

    if (file == NULL) {
        perror("getFile: could not open %s");
        return NULL;
    }
    if (buffer == NULL) {
        perror("getFile: could not allocate memory for buffer");
    }
    if (model == NULL) {
        perror("getFile: could not allocate memory to ARCHIVE");
    }
    if ((unzLocateFile(file, name, 0)) != UNZ_OK) {
        printf("getUnzip: error, could not find file in zip\n");
        free(buffer);
        free(model);
        return NULL;
    }
    if ((unzOpenCurrentFile(file)) != UNZ_OK) {
        printf("getUnzip: error, could not open current file in zip\n");
        free(buffer);
        free(model);
        return NULL;
    }

    while ((bytesRead = unzReadCurrentFile(file, buffer+length, 1024*sizeof(char))) > 0) {
        length += bytesRead;
        if (length >= capacity-1) {
            capacity *= 2;
            placeholder = (char*)realloc(buffer, (capacity+1)*sizeof(char));
            
            if (placeholder == NULL) {
                perror("getUnzip: could not resize buffer");
            } else {
                buffer = placeholder;
                placeholder = NULL;
            }
        }
    }

    if (length == 0) {
        printf("getUnzip: error reading file in zip\n");
        free(buffer);
        free(model);
        return NULL;
    }

    model->name = strdup(name);
    model->size = length;
    model->tab = buffer;

    returnString(&model->name, "name");
    unzCloseCurrentFile(file);
    return model;
}

//Read a file into a buffer and return the ARCHIVE pointer
ARCHIVE* getFile(const char* path) {
    char *buffer, *placeholder;
    size_t bytesRead, length = 0, capacity = 1024;
    FILE *file = fopen(path, "r");
    ARCHIVE* model = (ARCHIVE*)malloc(sizeof(ARCHIVE));
    buffer = (char*)calloc(1025, sizeof(char));

    if (file == NULL) {
        perror("getFile: could not open %s");
        return NULL;
    }
    if (buffer == NULL) {
        perror("getFile: could not allocate memory for buffer");
    }
    if (model == NULL) {
        perror("getFile: could not allocate memory to ARCHIVE");
    }

    while ((bytesRead = fread(buffer+length, sizeof(char), 1024, file)) > 0) {
        length += bytesRead;
        if (length >= capacity-1) {
            capacity *= 2;
            placeholder = (char*)realloc(buffer, (capacity+1)*sizeof(char));
            
            if (placeholder == NULL) {
                perror("getFile: could not resize buffer");
            } else {
                buffer = placeholder;
                placeholder = NULL;
            }
        }
    }

    placeholder = (char*)realloc(buffer, length*sizeof(char));
    if (placeholder != NULL) {
        buffer = placeholder;
    } else {
        perror("getFile: failed to shrink array");
        return NULL;
        placeholder = NULL;
    }

    model->name = strdup(path);
    returnString(&model->name, "name");
    model->size = length;
    model->tab = buffer;

    fclose(file);
    return model;
}

void addFile(FOLDER** folder, ARCHIVE* model) {
    if (folder[0]->count == 0) {
        folder[0]->content = (ARCHIVE*)calloc(folder[0]->file_capacity, sizeof(ARCHIVE));
        if (folder[0]->content == NULL) {
            perror("Error allocating memory for new file");
        }
    }
    if (folder[0]->count >= (folder[0]->file_capacity-1)) {
        folder[0]->file_capacity *= 2;
        folder[0]->content = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity*sizeof(ARCHIVE));
        if (folder[0]->content == NULL) {
            perror("Error reallocating memory for new file");
        }
    }
    folder[0]->content[folder[0]->count] = *model;
    folder[0]->count++;
}

ARCHIVE* searchFile(FOLDER* target, char* file) {
    size_t *position = calloc(8, sizeof(size_t)), navCount = 0;
    FOLDER* navigator = target;
    ARCHIVE* target_file = NULL;
    bool found = false;

    if (position == NULL) {
        perror("Error alocating memory for position array");
        return NULL;
    } else {
        for (int x = 0; x < 8; x++) {
            position[x] = 0;
        }
    }

    while (!found) {
        if (position[navCount] < navigator->subcount) {
            navigator = &navigator->subdir[position[navCount]];
            navCount++;
        } else if (position[navCount] == navigator->subcount) {
            for (int x = 0; navigator->count > 0 && x < (int)navigator->count; x++) {
                if (strcmp(navigator->content[x].name, file) == 0) {
                    target_file = &navigator->content[x];
                    found = true;
                    break;
                }
            }

        } else if (navigator->parent != NULL && navCount > 0) {
            position[navCount] = 0;
            navCount--;
            navigator = navigator->parent;
            navigator = &navigator->subdir[position[navCount]];
        }

        if (navigator->parent == NULL) {
            break;
        }
        position[navCount]++;
    }
    free(position);
    return target_file;
}

//Print lines that contain \n in the curses screen
size_t printLines(WINDOW* window, char* list, int y, int x) {
    size_t lenght, big = 0;
    char *pointer = strchr(list, '\n'), *checkpoint = &list[0], buffer[1025];

    while(pointer != NULL) {
        pointer = pointer + 1;
        lenght = pointer - checkpoint -1;
        if (lenght > big) {
            big = lenght;
        }
        strncpy(buffer, checkpoint, lenght);
        buffer[lenght] = '\0';
        mvwprintw(window, y, x, buffer);
        checkpoint = pointer;
        pointer = strchr(pointer, '\n');
        y++;
    }
    
    return big;
}

//Read from a target file into the memory
FOLDER* scanFolder(FOLDER* pack, char* path) {
    int dirNumber = 0, result = 0, type = 0, folderCursor = FOLDERCHUNK;
    long *dirPosition = (long*)calloc(folderCursor, sizeof(long));
    //struct stat status;
    struct dirent *entry;
    char placeholder[1024];

    if (dirPosition == NULL) {
        perror("Error allocating memory for directory cursor");
        free(dirPosition);
        return NULL;
    } else {
        for (int x = 0; x < folderCursor; x++) {
            dirPosition[x] = 2;
        }
    }

    DIR* scanner = opendir(path);
    if (scanner != NULL) {
        seekdir(scanner, 2);
        entry = readdir(scanner);
        strcpy(placeholder, entry->d_name);
        returnString(&path, placeholder);
    } else {
        printf("Error acessing %s: %s\n", path, strerror(errno));
        return NULL;
    }
    // strcpy(placeholder, entry->d_name);
    FOLDER* folder = createFolder(pack, placeholder);

    //Differentiate folder from zip file
    type = fileType(path);
    if ((type) == 0) {

        closedir(scanner);
        scanner = opendir(path);
        if (scanner != NULL) {
            seekdir(scanner, 2);
        } else {
            printf("Could not open %s: %s\n", path, strerror(errno));
            return NULL;
        }
        
        while(result == 0) {
            entry = readdir(scanner);

            //Return logic
            while(entry == NULL) {
                //Reached end of contents
                if ((dirPosition[dirNumber]-2) == (long)folder->subcount && folder->parent != NULL) {
                    if (folder->subcount > 0) {
                        FOLDER* temp = (FOLDER*)realloc(folder->subdir, folder->subcount*sizeof(FOLDER));
                        if (temp == NULL) {
                            printf("Error resizing folder %s: %s\n", folder->name, strerror(errno));

                            //Error logic
                        } else {
                            folder->subdir = temp;
                            temp = NULL;
                        }
                    }
                    if (folder->count > 0) {
                        ARCHIVE* temp = (ARCHIVE*)realloc(folder->content, folder->count*sizeof(ARCHIVE));
                        if (temp == NULL) {
                            printf("Error resizing archive %s: %s\n", folder->name, strerror(errno));

                            //Error logic
                        } else {
                            folder->content = temp;
                            temp = NULL;
                        }
                    }

                    dirPosition[dirNumber] = 2;
                    dirNumber--;
                    returnString(&path, "path");
                    folder = folder->parent;
                }
                //Entering new folder
                if ((dirPosition[dirNumber]-2) < (long)folder->subcount) {
                    folder = &folder->subdir[dirPosition[dirNumber]-2];
                    dirPosition[dirNumber]++;
                    dirNumber++;
                    returnString(&path, folder->name);

                }
                //End of loop
                if (dirNumber <= 0) {
                    result = 1;
                    break;
                }

                closedir(scanner);
                scanner = opendir(path);
                if (scanner != NULL) {
                    seekdir(scanner, dirPosition[dirNumber]);
                    entry = readdir(scanner);
                } else {
                    printf("scanFolder: error openning %s %s\n", path, strerror(errno));
                }
            }

            if ((result) == 1) {
                break;
            }

            strcpy(placeholder, entry->d_name);
            returnString(&path, placeholder);
            type = fileType(path);

            //0 is folder, 1 is file
            if (type == 0) {
                FOLDER* pointer = createFolder(folder, placeholder);
                addFolder(&folder, pointer);
                
                printf("FOLDER: <%s>\n", path);

            } else if (type == 1) {
                ARCHIVE* pointer = getFile(path);
                addFile(&folder, pointer);
                printf("FILE: <%s>\n", path);

            }
            returnString(&path, "path");
            
        } 
        return folder;
        
    } else if (type == 2) {
        closedir(scanner);
        unzFile rp = unzOpen(path);
        if (rp == NULL) {
            perror("Could not open zipfile");
            unzClose(rp);
            return NULL;
        }

        result = unzGoToFirstFile(rp);
        if (result != UNZ_OK) {
            perror("Could not go to first file");
            unzClose(rp);
            return NULL;
        }

        unz_file_info zip_entry;
        while (result == UNZ_OK) {
            result = unzGetCurrentFileInfo(rp, &zip_entry, placeholder, sizeof(placeholder), NULL, 0, NULL, 0);

            if (result != UNZ_OK) {
                printf("Error getting file info\n");
                return NULL;
            }

            char* temp = strdup(placeholder);
            returnString(&temp, "path");
            returnString(&temp, "name");
            while (strcmp(folder->name, temp) != 0 && dirNumber > 0) {
                if (folder->subcount > 0) {
                    folder->subdir = (FOLDER*)realloc(folder->subdir, folder->subcount*sizeof(FOLDER));
                    if (folder->subdir == NULL) {
                        perror("Could not realloc space when leaving folder");
                        return NULL;
                    }
                }
                if (folder->count > 0) {
                    folder->content = (ARCHIVE*)realloc(folder->content, folder->count*sizeof(ARCHIVE));
                    if (folder->content == NULL) {
                        perror("Could not realloc space when leaving folder");
                        return NULL;
                    }
                }
                folder = folder->parent;
                dirNumber--;
                printf("Return to %s\n", folder->name);
            }
            
            type = fileType(placeholder);
            if (type == 0) {
                FOLDER* pointer = createFolder(folder, placeholder);
                returnString(&pointer->name, "name");
                addFolder(&folder, pointer);
                folder = &folder->subdir[folder->subcount-1];
                dirNumber++;

                printf("FOLDER: <%s>\n", placeholder);
            } else if (type == 1) {
                ARCHIVE* pointer = getUnzip(rp, placeholder);
                addFile(&folder, pointer);

                printf("FILE: <%s>\n", placeholder);
            }

            result = unzGoToNextFile(rp);
        }
        return folder;
    } else if (type == 1) {
        while(entry != NULL) {
            strcpy(placeholder, entry->d_name);
            returnString(&path, "path");
            returnString(&path, placeholder);
            ARCHIVE* file = getFile(path);
            addFile(&folder, file);
            entry = readdir(scanner);
        }
        return folder;
    }
    return NULL;
}

int main () {
    char *path = calloc(PATH_MAX, sizeof(char));
    if (path == NULL) {
        perror("Error allocating memory for start path");
        return 1;
    }
    if ((getcwd(path, PATH_MAX)) == NULL) {
        perror("getwcd() error");
        return 1;
    }
    if (path != NULL) {
        printf("path = <%s>\n", path);
        // returnString(&path, "RP1");
    } else {
        printf("<%s>", path);
        perror("Invalid path");
        return 1;
    }

    //Getting logo
    returnString(&path, "lang");
    FOLDER* lang = scanFolder(NULL, path);
    FOLDER* targets = (FOLDER*)calloc(2, sizeof(FOLDER));
    returnString(&path, "path");

    size_t lenght;
    char *text, *logo = NULL, **entriesList = NULL, *pointer, **message, *temp, buffer[256];
    
    pointer = strstr(lang->content->tab, "[Options]:\n");
    lenght = pointer - lang->content->tab;
    logo = (char*)calloc(lenght+1, sizeof(char));
    if (logo != NULL) {
        strncpy(logo, lang->content->tab+8, lenght-8);
        logo[lenght-8] = '\0';
    } else {
        perror("Error when allocating memory for logo");
        return 1;
    }

    //Skipping "[Options]:\n" part and getting sidebar
    pointer = pointer + 11;
    lenght = pointer - lang->content->tab;
    text = (char*)calloc(lenght+1, sizeof(char));
    if (text != NULL) {
        strncpy(text, pointer, lenght);
    } else {
        perror("Error when allocating memory for sidebar text");
        return 1;
    }

    //Getting messages
    message = (char**)calloc(3, sizeof(char*));
    pointer = strstr(lang->content->tab, "[Messages]:\n");
    pointer = pointer+12;
    for (int x = 0; x < 3; x++) {
        temp = strchr(pointer, '\n');
        lenght = temp - pointer;
        temp = NULL;
        temp = (char*)calloc(lenght, sizeof(char));
        strncpy(temp, pointer, lenght-1);
        message[x] = temp;
        pointer += 1;
        pointer = strchr(pointer, '\n');
        pointer += 1;
        temp = NULL;
    }
    
    freeFolder(lang);
    
    //Starting the menu;
    initscr();
    int height = LINES, width = COLS, input = 0, cursor[2], optLenght;
    cursor[0] = cursor[1] = 0;
    bool quit = false;
    n_entries = 0;

    WINDOW* sidebar = newwin(height, 32, 0, 0);
    WINDOW* window = newwin(height, (width-32), 0, 32);
    WINDOW* subwin = derwin(window, (height-8), (width-32), 8, 0);
    refresh();
    curs_set(0);
    noecho();
    keypad(window, true);

    temp = strchr(logo, '\n');
    size_t center = temp - logo;
    center = (width*3/4) - center;
    center /= 2;

    box(sidebar, 0, 0);
    box(subwin, 0, 0);
    optLenght = (int)printLines(sidebar, text, 0, 1);
    printLines(window, logo, 0, center);
    wrefresh(sidebar);
    wrefresh(window);
    wrefresh(subwin);

    while (!quit) {
        switch (cursor[0])
        {  
        case 0:
            if (n_entries == 0) {
                getcwd(path, PATH_MAX);
                returnString(&path, "RP1");
                entriesList = (char**)calloc(8, sizeof(char*));

                struct dirent *entry;
                DIR* scan = opendir(path);
                seekdir(scan, 2);

                entry = readdir(scan);
                while (entry != NULL && n_entries < 8) {
                    strcpy(buffer, entry->d_name);
                    buffer[strlen(entry->d_name)] = '\0';
                    char* temp = strdup(buffer);
                    entriesList[n_entries] = temp;
                    n_entries++;
                    entry = readdir(scan);
                }
                if (entry == NULL) {
                    mvwprintw(subwin, 1, 1, message[0]);
                }
            }
            for (int x = 0; x < n_entries; x++) {
                mvwprintw(subwin, x+1, 1, entriesList[x]);
            }
            break;
        default:
            break;
        }

        switch (cursor[1])
        {
        case 0:
            mvwchgat(sidebar, cursor[0]+1, 1, optLenght, A_STANDOUT, 0, NULL);
            wrefresh(sidebar);
            break;
        case 1:
            mvwchgat(sidebar, cursor[0]+1, 1, optLenght, A_STANDOUT, 0, NULL);
            mvwchgat(subwin, cursor[0]+1, 1, strlen(entriesList[cursor[0]]), A_STANDOUT, 0, NULL);
            wrefresh(sidebar);
            break;
        default:
            break;
        }

        input = wgetch(window);

        switch (input)
        {
        case KEY_DOWN:
            if (cursor[0] < 2) {
                mvwchgat(sidebar, cursor[0]+1, 1, optLenght, A_NORMAL, 0, NULL);
                cursor[0]++;
            }
            break;
        case KEY_UP:
            if (cursor[0] > 0) {
                mvwchgat(sidebar, cursor[0]+1, 1, optLenght, A_NORMAL, 0, NULL);
                cursor[0]--;
            }
            break;
        case ENTER:
            switch (cursor[0])
            {
            case 2:
                quit = true;
                break;
            }
            break;
        case TAB:
            cursor[1] = !cursor[1];
            break;
        case KEY_RESIZE:
            resize_term(0, 0);
            endwin();
            refresh();
            clear();
            wclear(window);
            wclear(sidebar);
            wclear(subwin);

            height = LINES;
            width = COLS;

            resize_window(sidebar, height, 32);
            resize_window(window, height, (width-32));
            resize_window(subwin, (height-8), (width-32));
            box(subwin, 0, 0);
            box(sidebar, 0, 0);
            optLenght = (int)printLines(sidebar, text, 0, 1);
            temp = strchr(logo, '\n');
            center = temp - logo;
            center = (width-32) - center;
            center /= 2;
            printLines(window, logo, 0, center);

            wrefresh(sidebar);
            wrefresh(window);
            wrefresh(subwin);

            break;
        
        default:
            break;
        }
    }

    //Free query
    free(text);
    free(logo);
    for (int x = 0; x < 3; x++) {
        free(message[x]);
    }
    delwin(window);
    delwin(sidebar);
    delwin(subwin);
    freeFolder(lang);
    freeFolder(targets);
    endwin();

    return 0;
}