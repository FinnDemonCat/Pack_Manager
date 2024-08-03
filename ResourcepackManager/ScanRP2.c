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
#include <math.h>
#include <time.h>

#define ENTER 10
#define TAB '\t'
#define FOLDERCHUNK 16
#define FILECHUNK 64

int n_entries = 0;

typedef struct QUEUE {
    int *value;
    int end;
    char** item;
    size_t size;
} QUEUE;

typedef struct ARCHIVE {
    char* tab;
    char* name;
    size_t size;
} ARCHIVE;

typedef struct FOLDER {
    char* name;
    struct FOLDER* parent;
    struct FOLDER* subdir;
    size_t subcount;
    size_t count;
    size_t capacity;
    size_t file_capacity;
    size_t n_files;
    size_t n_folders;
    ARCHIVE* content;
} FOLDER;

typedef struct RESOLUTION {
    WINDOW* window;
    int parameters[4][7];
    int y, x, size_y, size_x;
} RESOLUTION;

WINDOW* sidebar;
WINDOW* window;
WINDOW* miniwin;
WINDOW* action;

RESOLUTION* _sidebar;
RESOLUTION* _window;
RESOLUTION* _miniwin;
RESOLUTION* _action;

FOLDER* lang;
QUEUE* entries;
QUEUE* query;

char** translated;
char* report = NULL;
size_t report_size = 1024, report_end = 0, report_lenght = 0;

QUEUE* initQueue(size_t size) {
    QUEUE* queue = malloc(sizeof(QUEUE));
    queue->value = (int*)calloc(size, sizeof(int));
    for (int x = 0; x < (int)size; x++) {
        queue->value[x] = 0;
    }
    
    queue->item = (char**)calloc(size, sizeof(char*));
    for (int x = 0; x < (int)size; x++) {
        queue->item[x] = (char*)calloc(PATH_MAX+1, sizeof(char));
        queue->value[x] = 0;
    }
    queue->end = 0;
    queue->size = size;
    return queue;
}

void enQueue(QUEUE* queue, char* item) {
    if (queue->end == (int)queue->size) {
        return;
    }
    strncpy(queue->item[queue->end], item, PATH_MAX);
    queue->end++;
}

void peekQueue(WINDOW* window, int y, int x, QUEUE* queue) {
    for (int i = 0; i < queue->end; i++) {
        mvwprintw(window, y+i, x, "%s [%d]", queue->item[i], queue->value[i]);
    }
}

void deQueue(QUEUE* queue, int item) {
    for (int x = item; x < queue->end-1; x++) {
        strncpy(queue->item[x], queue->item[x+1], PATH_MAX);
        queue->value[x] = queue->value[x+1];
    }
    queue->end--;
}

void endQueue(QUEUE* queue) {
    free(queue->value);
    for (;queue->end > 0; queue->end--) {
        free(queue->item[queue->end-1]);
    }
    free(queue->item);
    free(queue);
}

void logger(char* format, ...) {
    char* buffer;
    size_t size;
    va_list arguments, reflection;
    va_start(arguments, format);
    va_copy(reflection, arguments);

    size = vsnprintf(NULL, 0, format, arguments);
    report_lenght += size + 1;
    if ((report_lenght + size) > (report_size - 2)) {
        report_size *= 2;
        report = (char*)realloc(report, report_size * sizeof(char));
    }

    buffer = (char*)calloc((size + 1), sizeof(char));
    vsnprintf(buffer, size + 1, format, arguments);

    va_end(arguments);
    va_end(reflection);
    strcat(report, buffer);
    report_end++;

    free(buffer);
}

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
            logger("Could not reallcate memory for new path string, %s. Using backup\n", strerror(errno));

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

void printLog(char* path) {
    char date[1024];

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    sprintf(date, "%s\\log\\log_%02d-%02d-%02d.txt", path, tm.tm_hour, tm.tm_min, tm.tm_sec);
    
    FILE* reporting = fopen(date, "w+");
    fprintf(reporting, report);
    fclose(reporting);
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
                logger("%s isn't a directorr nor file \n", path);
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
        logger("%s\n", strerror(errno));
        return -1;
    }
    return -1;
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

FOLDER* createFolder(FOLDER* parent, const char* name) {
    FOLDER* folder = (FOLDER*)malloc(sizeof(FOLDER));
    if (folder == NULL) {
        logger("Error creating a new folder in memmor, %s\n", strerror(errno));
        return NULL;
    }
    folder->name = strdup(name);
    folder->content = NULL;
    folder->subdir = NULL;
    folder->count = folder->subcount = folder->n_folders = folder->n_files = 0;
    folder->capacity = FOLDERCHUNK;
    folder->file_capacity = FILECHUNK;
    folder->parent = parent;
    
    return folder;
}

void addFolder(FOLDER** parent, FOLDER* subdir) {
    if (parent[0]->subcount == 0) {
        parent[0]->subdir = (FOLDER*)calloc(parent[0]->capacity, sizeof(FOLDER));

        if (parent[0]->subdir == NULL) {
            logger("Error allocating memory for new folder, %s\n", strerror(errno));
        }
    } else if (parent[0]->subcount >= parent[0]->capacity) {
        parent[0]->capacity *= 2;
        logger("Reallocating more memory for folder [%s], %d\n", parent[0]->subdir->name, (int)parent[0]->capacity);
        parent[0]->subdir = (FOLDER*)realloc(parent[0]->subdir, (int)parent[0]->capacity*sizeof(FOLDER));

        if (parent[0]->subdir == NULL) {
            logger("Error reallocating memory for new folder, %s\n", strerror(errno));
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
        logger("getUnzip: Could not open, %s\n", strerror(errno));
        return NULL;
    }
    if (buffer == NULL) {
        logger("getUnzip: Could allocate memory for buffer, %s\n", strerror(errno));
    }
    if (model == NULL) {
        logger("getUnzip: Could allocate memory to ARCHIVE, %s\n", strerror(errno));
    }
    if ((unzLocateFile(file, name, 0)) != UNZ_OK) {
        logger("getUnzip: Error, could not find file %s in zip\n", name);
        free(buffer);
        free(model);
        return NULL;
    }
    if ((unzOpenCurrentFile(file)) != UNZ_OK) {
        logger("getUnzip: Error, could not open current file in zip\n");
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
                logger("getUnzip: Error, could not resize buffer, %s\n", strerror(errno));
                free(buffer);
            } else {
                buffer = placeholder;
                placeholder = NULL;
            }
        }
    }

    if (length == 0) {
        logger("getUnzip: Error reading file in zip, %s\n", strerror(errno));
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
        logger("getFile: Could not open %s due to %s\n", path, strerror(errno));
        return NULL;
    }
    if (buffer == NULL) {
        logger("getFile: Could not allocate memory for buffer, %s\n", strerror(errno));
    }
    if (model == NULL) {
        logger("getFile: Could not allocate memory to ARCHIVE, %s\n", strerror(errno));
    }

    while ((bytesRead = fread(buffer+length, sizeof(char), 1024, file)) > 0) {
        length += bytesRead;
        if (length >= capacity-1) {
            capacity *= 2;
            placeholder = (char*)realloc(buffer, (capacity+1)*sizeof(char));
            
            if (placeholder == NULL) {
                logger("getFile: Could not resize buffer, %s\n", strerror(errno));
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
        logger("getFile: Could realloc string, %s\n", strerror(errno));
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
            logger("Error allocating memory for new file, %s\n", strerror(errno));
        }
    }
    if (folder[0]->count >= (folder[0]->file_capacity-1)) {
        folder[0]->file_capacity *= 2;
        folder[0]->content = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity*sizeof(ARCHIVE));
        if (folder[0]->content == NULL) {
            logger("Error reallocating memory for new file, %s\n", strerror(errno));
        }
    }
    folder[0]->content[folder[0]->count] = *model;
    folder[0]->count++;
}


char** getLang(FOLDER* lang, int file) {
    char *pointer, *checkpoint, *temp, **translated = (char**)calloc(3, sizeof(char*));
    size_t lenght;

    //First options
    //Second messages
    //Third Logo

    //Getting logo
    pointer = strstr(lang->content[file].tab, "[Options]");
    checkpoint = &lang->content[file].tab[0];
    checkpoint += 8;
    lenght = (pointer - checkpoint) + 1;

    temp = (char*)calloc(lenght, sizeof(char));
    strncpy(temp, checkpoint, lenght-1);

    translated[2] = temp;
    temp = NULL;

    //Getting Options
    pointer += 11;
    checkpoint = pointer;
    pointer = strstr(pointer, "[Messages]");
    lenght = (pointer - checkpoint) + 1;

    temp = (char*)calloc(lenght, sizeof(char));
    strncpy(temp, checkpoint, lenght-1);

    translated[0] = temp;
    temp = NULL;

    //Getting Messages
    pointer += 12;
    lenght = (strlen(lang->content[file].tab) + 1) - (pointer - lang->content[file].tab);
    temp = (char*)calloc(lenght, sizeof(char));
    strncpy(temp, pointer, lenght-1);

    translated[1] = temp;
    temp = NULL;
    
    return translated;
}

//Print lines that contain \n in the curses screen
size_t printLines(WINDOW* window, char* list, int y, int x, int line) {
    size_t lenght, big = 0;
    char *pointer = list, *checkpoint = list;

    if (line > 0) {
        for (int x = 1; x < line && pointer != NULL; x++) {
            pointer = strchr(pointer, '\n');
            pointer += 1;
        }
        checkpoint = pointer;
        pointer = strchr(pointer, '\n');
        pointer += 1;
        lenght = pointer - checkpoint;
        mvwprintw(window, y, x, "%.*s", lenght-1, checkpoint);

    } else {
        while ((pointer = strchr(pointer, '\n')) != NULL && line == 0) {
            pointer += 1;
            lenght = pointer - checkpoint;
            mvwprintw(window, y, x, "%.*s", lenght-1, checkpoint);
            y++;
            checkpoint = pointer;
            if (big < lenght) {
                big = lenght;
            }
        }
    }
    return big;
}

//Prepares the confirmation dialog screen with the gives line from lang file.
//Type 0 will prepare a yes/no question. Type 1 will prepare a notice
void confirmationDialog(char* lang, int line, int* sizes, int type) {
    size_t center, size;
    char *pointer = lang, *checkpoint, *checkpoint2;
    box(action, 0, 0);
    center = getmaxx(action);

    for (int x = 1; x < line; x++, pointer += 1) {
        pointer = strchr(pointer, '\n');
    }
    checkpoint = pointer;
    pointer = strchr(pointer, '\n');
    pointer += 1;
    size = pointer - checkpoint;
    
    if (size < center - 2) {
        mvwprintw(action, 1, ((center - size - 1)/2), "%.*s", size - 1, checkpoint);
    } else {
        checkpoint2 = &checkpoint[center - 2];
        for (int x = (center - 2); *checkpoint2 != ' '; x--) {
            checkpoint2 = &checkpoint[x];
        }

        mvwprintw(action, 1, ((center - (checkpoint2 - checkpoint - 1))/2), "%.*s", (checkpoint2 - checkpoint - 1), checkpoint);
        mvwprintw(action, 2, ((center - (pointer - checkpoint2 - 1))/2), "%.*s", (pointer - checkpoint2 - 1), checkpoint2);
    }

    if (type == 0) {
        pointer = lang;
        for (int x = 0; x < 6; x++, pointer++) {
            pointer = strchr(pointer, '\n');
        }

        //Get Lang "YES"
        checkpoint = pointer;
        pointer = strchr(pointer, '\n');
        pointer += 1;

        size = pointer - checkpoint - 1;
        sizes[0] = size;
        mvwprintw(action, getmaxy(action) - 2, ((center - size)/4), "%.*s", size, checkpoint);

        //Get Lang "NO"
        checkpoint = pointer;
        pointer = strchr(pointer, '\n');
        pointer += 1;

        size = pointer - checkpoint - 1;
        sizes[1] = size;

        mvwprintw(action, getmaxy(action) - 2, ((center - size)*3/4), "%.*s", size, checkpoint);
    } else {
        pointer = lang;
        for (int x = 0; x < 8; x++, pointer++) {
            pointer = strchr(pointer, '\n');
        }

        checkpoint = pointer;
        pointer = strchr(pointer, '\n');
        pointer += 1;
        size = pointer - checkpoint - 1;

        sizes[0] = size;
        mvwprintw(action, getmaxy(action) - 2, ((center - size)/2), "%.*s", size, checkpoint);
    }
}

void setWindowRatio(RESOLUTION* target, char* ratios) {
    char buffer[4][64], placeholder[2][32], operator, operator2, *pointer, *checkpoint;
    int alpha, beta, gamma, delta;
    int epsilon, zeta, eta;
    
    pointer = checkpoint = ratios;
    pointer = strchr(pointer, ',');
    
    for (int x = 0; pointer != NULL; x++) {
        pointer += 2;
        strncpy(buffer[x], checkpoint, (pointer - checkpoint - 2));
        buffer[x][(pointer - checkpoint - 2)] = '\0';
        checkpoint = pointer;
        pointer = strchr(pointer, ',');
    }
    strncpy(buffer[3], checkpoint, 63);

    for (int x = 0; x < 4; x++) {
        alpha = beta = gamma = delta = epsilon = zeta = eta = 0;
        placeholder[0][0] = '\0';
        placeholder[1][0] = '\0';
        operator = operator2 = ' ';

        pointer = buffer[x];
        checkpoint = pointer + 1;
        if ((pointer = strchr(pointer, ')')) != NULL) {
            strncpy(placeholder[0], checkpoint, (pointer - checkpoint));
            placeholder[0][pointer - checkpoint] = '\0';
        }

        if ((pointer += 1) != NULL) {
            operator = *pointer;
        }

        if ((checkpoint = strchr(checkpoint, '(')) != NULL && (pointer = strrchr(pointer, ')')) != NULL) {
            checkpoint += 1;
            strncpy(placeholder[1], checkpoint, (pointer - checkpoint));
            placeholder[1][pointer - checkpoint] = '\0';
        }

        if ((strstr(placeholder[0], "LINES")) != NULL) {
            alpha = -1;
            sscanf(placeholder[0], "%*s %c %d", &operator2, &beta);
        } else if ((strstr(placeholder[0], "COLS")) != NULL) {
            alpha = -1;
            sscanf(placeholder[0], "%*s %c %d", &operator2, &beta);
        } else {
            sscanf(placeholder[0], "%d %c %d", &alpha, &operator2, &beta);
        }

        if ((operator2 = strchr(placeholder[0], '+') != NULL)) {
            epsilon = 0;
        } else if ((operator2 = strchr(placeholder[0], '-') != NULL)) {
            epsilon = 1;
        } else if ((operator2 = strchr(placeholder[0], '/') != NULL)) {
            epsilon = 2;
        } else if ((operator2 = strchr(placeholder[0], '*') != NULL)) {
            epsilon = 3;
        } else {
            epsilon = -1;
        }

        if ((strstr(placeholder[1], "LINES")) != NULL) {
            gamma = LINES;
            sscanf(placeholder[1], "%*s %c %d", &operator2, &delta);
        } else if ((strstr(placeholder[1], "COLS")) != NULL) {
            gamma = COLS;
            sscanf(placeholder[1], "%*s %c %d", &operator2, &delta);
        } else {
            sscanf(placeholder[1], "%d %c %d", &gamma, &operator2, &delta);
        }
        if ((operator2 = strchr(placeholder[1], '+') != NULL)) {
            zeta = 0;
        } else if ((operator2 = strchr(placeholder[1], '-') != NULL)) {
            zeta = 1;
        } else if ((operator2 = strchr(placeholder[1], '/') != NULL)) {
            zeta = 2;
        } else if ((operator2 = strchr(placeholder[1], '*') != NULL)) {
            zeta = 3;
        } else {
            zeta = -1;
        }

        if (operator == '+') {
            eta = 0;
        } else if (operator == '-') {
            eta = 1;
        } else if (operator == '/') {
            eta = 2;
        } else if (operator == '*') {
            eta = 3;
        } else {
            eta = -1;
        }

        target->parameters[x][0] = alpha;
        target->parameters[x][1] = epsilon;
        target->parameters[x][2] = beta;
        target->parameters[x][3] = eta;
        target->parameters[x][4] = gamma;
        target->parameters[x][5] = zeta;
        target->parameters[x][6] = delta;
    }

}

void calcWindow(RESOLUTION* target) {
    int *variables[4], gamma[2];
    int alpha, beta; //variables
    int delta; //operator

    variables[0] = &target->size_y;
    variables[1] = &target->size_x;
    variables[2] = &target->y;
    variables[3] = &target->x;

    for (int x = 0; x < 4; x++) {
        gamma[0] = gamma[1] = 0;
        alpha = beta = delta = 0;
        for (int y = 0, z = 0; y < 7; y+= 4, z++) {
            alpha = target->parameters[x][y];
            delta = target->parameters[x][y+1];
            beta = target->parameters[x][y+2];

            if ((x % 2) == 0) {
                if (alpha == -1) {
                    alpha = LINES;
                }
            } else {
                if (alpha == -1) {
                    alpha = COLS;
                }
            }


            switch (delta)
            {
            case 0:
                gamma[z] = alpha + beta;
                break;
            case 1:
                gamma[z] = alpha - beta;
                break;
            case 2:
                gamma[z] = alpha / beta;
                break;
            case 3:
                gamma[z] = alpha * beta;
                break;
            default:
                gamma[z] = alpha;
                break;
            }
        }

        delta = target->parameters[x][3];

        switch (delta)
        {
        case 0:
            gamma[0] += gamma[1];
            break;
        case 1:
            gamma[0] -= gamma[1];
            break;
        case 2:
            gamma[0] /= gamma[1];
            break;
        case 3:
            gamma[0] *= gamma[1];
            break;
        }

        *variables[x] = gamma[0];
    }
    
}

void updateWindows() {
    char* temp;
    int result;

    resize_term(0, 0);
    endwin();
    refresh();
    clear();

    wclear(action);
    wclear(sidebar);
    wclear(window);
    wclear(miniwin);

    calcWindow(_window);
    calcWindow(_sidebar);
    calcWindow(_miniwin);
    calcWindow(_action);

    resize_window(window, _window->size_y, _window->size_x);
    resize_window(sidebar, _sidebar->size_y, _sidebar->size_x);
    resize_window(miniwin, _miniwin->size_y, _miniwin->size_x);
    resize_window(action, _action->size_y, _action->size_x);

    mvwin(action, _action->y, _action->x);

    box(miniwin, 0, 0);
    box(sidebar, 0, 0);
    
    temp = strchr(translated[2], '\n');
    result = temp - translated[2];
    result = (COLS-32) - result;
    result /= 2;

    printLines(sidebar, translated[0], 0, 1, 0);
    printLines(window, translated[2], 0, result, 0);

    wrefresh(action);
    wrefresh(miniwin);
    wrefresh(window);
    wrefresh(sidebar);
}

//Read from a target file into the memory
//pack is the parent folder, path is the path to loop for and position is the file position. -1 Will take the path as target.
FOLDER* scanFolder(FOLDER* pack, char* path, int position) {
    int dirNumber = 0, result = 0, type = 0, folderCursor = FOLDERCHUNK, line_number = 0, input, folder_count = 0, file_count = 0;
    long *dirPosition = (long*)calloc(folderCursor, sizeof(long));
    struct dirent *entry;
    char placeholder[1024], *location = strdup(path), name[1024];

    if (translated != NULL) {
        updateWindows();
        nodelay(window, true);
    }

    if (dirPosition == NULL) {
        logger("%s: Error allocating memory for cursor,  %s\n", strerror(errno));

        return NULL;
    } else {
        for (int x = 0; x < folderCursor; x++) {
            dirPosition[x] = 2;
        }
    }

    DIR* scanner = opendir(location);
    if (scanner != NULL) {
        if (position == -1) {
            seekdir(scanner, 2);
        } else {
            seekdir(scanner, position + 2);
        }
        entry = readdir(scanner);
        strcpy(placeholder, entry->d_name);
        returnString(&location, placeholder);

        logger("Scanning: %s\n", placeholder);

    } else {
        logger("%s: Error acessing:  %s\n", placeholder);

        return NULL;
    }
    wrefresh(miniwin);
    FOLDER* folder = createFolder(pack, placeholder);

    strcpy(name, placeholder);
    //Differentiate folder from zip file
    type = fileType(location);
    if ((type) == 0) {
        logger("Started the scan process. The target is a folder\n");

        //Create scoll function and input switch
        line_number++;

        closedir(scanner);
        scanner = opendir(location);
        if (scanner != NULL) {
            seekdir(scanner, 2);
        } else {
            logger("%s: Could not open target, %s\n", name, strerror(errno));
            return NULL;
        }
        
        while(result == 0) {
            input = wgetch(window);

            wclear(miniwin);
            if (line_number < (getmaxy(miniwin) - 2)) {
                for (int x = report_end - line_number, y = 1; x < (int)report_end && x > -1; x++, y++) {
                    printLines(miniwin, report, y, 1, (x + 1));
                }
            } else {
                for (int x = report_end - (getmaxy(miniwin) - 2), y = 1; x < (int)report_end && x > -1 && y < (getmaxy(miniwin) - 1); x++, y++) {
                    printLines(miniwin, report, y, 1, (x + 1));
                }
            }
            box(miniwin, 0, 0);
            wrefresh(miniwin);

            if (input == KEY_RESIZE) {
                updateWindows();
            }

            entry = readdir(scanner);

            //Return logic
            while(entry == NULL) {
                //Reached end of contents
                if ((dirPosition[dirNumber]-2) == (long)folder->subcount && folder->parent != NULL) {
                    if (folder->subcount > 0) {
                        FOLDER* temp = (FOLDER*)realloc(folder->subdir, folder->subcount*sizeof(FOLDER));
                        if (temp == NULL) {
                            logger("%s: Error resizing folder %s, %s, %s\n", name, folder->name, strerror(errno));
                            line_number++;

                            //Error logic
                        } else {
                            folder->subdir = temp;
                            temp = NULL;
                        }
                    }
                    if (folder->count > 0) {
                        ARCHIVE* temp = (ARCHIVE*)realloc(folder->content, folder->count*sizeof(ARCHIVE));
                        if (temp == NULL) {
                            logger("%s: Error resizing archive %s, %s\n", name, folder->name, strerror(errno));
                            line_number++;

                            //Error logic
                        } else {
                            folder->content = temp;
                            temp = NULL;
                        }
                    }

                    dirPosition[dirNumber] = 2;
                    dirNumber--;
                    returnString(&location, "path");
                    logger("%s: Returning to <%s>\n", name, folder->name);
                    folder = folder->parent;
                }
                //Entering new folder
                if ((dirPosition[dirNumber]-2) < (long)folder->subcount) {
                    folder = &folder->subdir[dirPosition[dirNumber]-2];
                    dirPosition[dirNumber]++;
                    dirNumber++;
                    returnString(&location, folder->name);

                }
                //End of loop
                if (dirNumber <= 0) {
                    result = 1;
                    break;
                }

                closedir(scanner);
                scanner = opendir(location);
                if (scanner != NULL) {
                    seekdir(scanner, dirPosition[dirNumber]);
                    entry = readdir(scanner);
                } else {
                    logger("scanFolder: error openning %s: %s\n", location, strerror(errno));
                    line_number++;
                }
            }

            if ((result) == 1) {
                break;
            }

            strcpy(placeholder, entry->d_name);
            returnString(&location, placeholder);
            type = fileType(location);

            //0 is folder, 1 is file
            if (type == 0) {
                FOLDER* pointer = createFolder(folder, placeholder);
                addFolder(&folder, pointer);
                
                logger("%s: FOLDER: <%s>\n", name, location);
                folder_count++;
                line_number++;
            } else if (type == 1) {
                ARCHIVE* pointer = getFile(location);
                addFile(&folder, pointer);

                logger("%s: FILE: <%s>\n", name, location);
                file_count++;
                line_number++;
            }

            returnString(&location, "path");
            wrefresh(miniwin);
        }

        nodelay(window, false);

        free(location);
        pack->subcount++;
        folder->n_files = file_count;
        folder->n_folders = folder_count;
        logger("%s File Count: %d\n%s Folders Count: %d\n", name, folder->n_files, name, folder->n_folders);
        return folder;
        
    } else if (type == 2) {

        closedir(scanner);
        unzFile rp = unzOpen(location);
        if (rp == NULL) {
            logger("%s: Could not open zip folder, %s\n", name, strerror(errno));

            wrefresh(miniwin);
            unzClose(rp);
            return NULL;
        }

        result = unzGoToFirstFile(rp);
        if (result != UNZ_OK) {
            logger("%s: Could not go to first file, %s\n", name, strerror(errno));

            wrefresh(miniwin);
            unzClose(rp);
            return NULL;
        } //546

        unz_file_info zip_entry;
        while (result == UNZ_OK) {
            input = wgetch(window);

            wclear(miniwin);
            if (line_number < (getmaxy(miniwin) - 2)) {
                for (int x = report_end - line_number, y = 1; x < (int)report_end && x > -1; x++, y++) {
                    printLines(miniwin, report, y, 1, (x + 1));
                }
            } else {
                for (int x = report_end - (getmaxy(miniwin) - 2), y = 1; x < (int)report_end && x > -1 && y < (getmaxy(miniwin) - 1); x++, y++) {
                    printLines(miniwin, report, y, 1, (x + 1));
                }
            }
            box(miniwin, 0, 0);
            wrefresh(miniwin);

            if (input == KEY_RESIZE) {
                updateWindows();
            }

            input = 0;
            result = unzGetCurrentFileInfo(rp, &zip_entry, placeholder, sizeof(placeholder), NULL, 0, NULL, 0);

            if (result != UNZ_OK) {
                logger("%s: Error getting file info, %s\n", name, strerror(errno));
                
                wrefresh(miniwin);
                return NULL;
            }

            char* temp = strdup(placeholder);
            returnString(&temp, "path");
            returnString(&temp, "name");
            while (strcmp(folder->name, temp) != 0 && dirNumber > 0) {
                if (folder->subcount > 0) {
                    folder->subdir = (FOLDER*)realloc(folder->subdir, folder->subcount*sizeof(FOLDER));
                    if (folder->subdir == NULL) {
                        logger("%s: Could not realloc space when leaving folder, <%s>\n", name, strerror(errno));

                        wrefresh(miniwin);
                        return NULL;
                    }
                }
                if (folder->count > 0) {
                    folder->content = (ARCHIVE*)realloc(folder->content, folder->count*sizeof(ARCHIVE));
                    if (folder->content == NULL) {
                        logger("%s: Could not realloc space for archive when leaving folder, %s\n", name, strerror(errno));

                        wrefresh(miniwin);
                        return NULL;
                    }
                }
                
                folder = folder->parent;
                dirNumber--;

                logger("%s: Return to <%s>\n", name, folder->name);
                
                line_number++;
                wrefresh(miniwin);
            }
            
            type = fileType(placeholder);
            if (type == 0) {
                FOLDER* pointer = createFolder(folder, placeholder);
                returnString(&pointer->name, "name");
                addFolder(&folder, pointer);
                folder = &folder->subdir[folder->subcount-1];
                dirNumber++;

                logger("%s: FOLDER: <%s>\n", name, placeholder);
                folder_count++;
                line_number++;
            } else if (type == 1) {
                ARCHIVE* pointer = getUnzip(rp, placeholder);
                addFile(&folder, pointer);

                logger("%s: FILE: <%s>\n", name, placeholder);
                file_count++;
                line_number++;
            }

            free(temp);
            result = unzGoToNextFile(rp);
        }

        nodelay(window, false);


        pack->subcount++;
        free(location);
        folder->n_files = file_count;
        folder->n_folders = folder_count;
        logger("%s File Count: %d\n%s Folders Count: %d\n", name, folder->n_files, name, folder->n_folders);
        return folder;
    } else if (type == 1) {
        while(entry != NULL) {
            strcpy(placeholder, entry->d_name);
            returnString(&location, "path");
            returnString(&location, placeholder);
            ARCHIVE* file = getFile(location);
            addFile(&folder, file);
            entry = readdir(scanner);
        }

        free(location);
        return folder;
    }
    return NULL;
}

void overrideFiles(FOLDER* folder1, FOLDER* folder2) {
    bool firstTarget = false, secondTarget = false;
    ARCHIVE **target_list, **overrides_list;
    FOLDER *navigator;
    int position[16], dirNumber = 0, list_counter = 0;
    target_list = (ARCHIVE**)calloc(folder1->n_files, sizeof(ARCHIVE*));
    //overrides_list = (ARCHIVE**)calloc(folder1->n_files, sizeof(ARCHIVE*));

    for (int x = 0; x < 16; x++) {
        position[x] = 0;
    }

    for (int x = 0; x < (int)folder1->count; x++) {
        if (strcmp(folder1->content[x].name, "pack.mcmeta") == 0) {
            logger("overrideFiles %s is a resourcepack", folder1->name);
            firstTarget = true;
            break;
        } else {
            logger("overrideFiles %s isn't a resourcepack", folder1->name);
            break;
        }
    }

    for (int x = 0; x < (int)folder2->count; x++) {
        if (strcmp(folder2->content[x].name, "pack.mcmeta") == 0) {
            logger("overrideFiles %s is a resourcepack", folder2->name);
            secondTarget = true;
        } else {
            logger("overrideFiles %s isn't a resourcepack", folder2->name);
        }
    }
    
    navigator = folder1;
    while (true) {
        while (navigator->subcount > 0) {
            dirNumber++;
            navigator = &navigator->subdir[position[dirNumber]];
        }
        while (position[dirNumber] > (int)navigator->parent->subcount - 1 && dirNumber > 0) {
            navigator = navigator->parent;
            position[dirNumber] = 0;
            dirNumber--;
            position[dirNumber]++;
        }

        if (dirNumber <= 0) {
            break;
        }
        navigator = &navigator->parent->subdir[position[dirNumber]];
        position[dirNumber]++;

        for (int x = 0; x < (int)navigator->count; x++, list_counter++) {
            target_list[list_counter] = &navigator->content[x];
        }
    }
}

int main () {
    char *path = calloc(PATH_MAX, sizeof(char));
    report = (char*)calloc(report_size, sizeof(char));

    if (path == NULL) {
        logger("Error allocating memory for start path, %s\n", strerror(errno));
        return 1;
    }
    if ((getcwd(path, PATH_MAX)) == NULL) {
        logger("getwcd() error, %s\n", strerror(errno));
        return 1;
    }
    if (path != NULL) {
        logger("Location Path <%s>\n", path);
    } else {
        logger("Invalid Path <%s>, %s\n", path, strerror(errno));
        return 1;
    }

   
    if (report != NULL) {
        logger("Memory alloc for report was sucessfull\n");
    } else {
        logger("Could not alloc memory for report, aborting\n");
        return 1;
    }

    //Scanning the lang folder
    returnString(&path, "lang");
    lang = scanFolder(NULL, path, -1);
    returnString(&path, "path");
    returnString(&path, "resourcepacks");

    FOLDER* targets = createFolder(NULL, "targets");
    targets->subdir = (FOLDER*)calloc(2, sizeof(FOLDER));

    //Getting lang file
    translated = getLang(lang, 0);
        
    //Starting the menu;
    initscr();
    int input = 0, cursor[3], optLenght, actionLenght[2], type = 0, relay_message = -1;
    cursor[0] = cursor[1] = cursor[2] = 0;
    bool quit = false, update = true;
    n_entries = 0;

    resize_term(0, 0);

    logger("Allocating memory for windows\n");
    _sidebar = (RESOLUTION*)malloc(sizeof(RESOLUTION));
    _window = (RESOLUTION*)malloc(sizeof(RESOLUTION));
    _miniwin = (RESOLUTION*)malloc(sizeof(RESOLUTION));
    _action = (RESOLUTION*)malloc(sizeof(RESOLUTION));

    setWindowRatio(_sidebar, "(LINES), (32), (0), (0)");
    calcWindow(_sidebar);

    setWindowRatio(_window, "(LINES), (COLS - 32), (0), (32)");
    calcWindow(_window);

    setWindowRatio(_miniwin, "(LINES - 8), (COLS - 32), (8), (0)");
    calcWindow(_miniwin);

    setWindowRatio(_action, "(8), (36), (LINES - 8)/(2), (COLS - 36)/(2)");
    calcWindow(_action);
    
    sidebar = newwin(_sidebar->size_y, _sidebar->size_x, _sidebar->y, _sidebar->x);
    window = newwin(_window->size_y, _window->size_x, _window->y, _window->x);
    miniwin = derwin(window, _miniwin->size_y, _miniwin->size_x, _miniwin->y, _miniwin->x);
    action = newwin(_action->size_y, _action->size_x, _action->y, _action->x);

    refresh();
    curs_set(0);
    noecho();
    keypad(window, true);
    
    updateWindows();
    optLenght = printLines(NULL, translated[0], 0, 1, 0);

    query = initQueue(8);
    entries = initQueue(8);

    logger("Starting screen\n");
    while (!quit) {
        //Draw miniwindow
        if (update == true) {
            wclear(miniwin);
            box(miniwin, 0, 0);

            switch (cursor[1])
            {
            case 0:
                if (entries->end < 1) {
                    struct dirent *entry;
                    DIR* scan = opendir(path);
                    seekdir(scan, 2);

                    entry = readdir(scan);
                    if (entry == NULL) {
                        printLines(miniwin, translated[1], 1, 1, 1);
                    }
                    while (entry != NULL && n_entries < 8) {
                        enQueue(entries, entry->d_name);
                        n_entries++;
                        entry = readdir(scan);
                    }
                }

                if (entries->end > 0) {
                    for (int x = 0; x < entries->end; x++) {
                        mvwprintw(miniwin, 1+x, 1, "%s [%c]  ", entries->item[x], (entries->value[x] == 1) ? 'X' : ' ');
                    }
                    for (int x = 0; x < query->end; x ++) {
                        mvwprintw(miniwin, 1 + query->value[x], strlen(entries->item[query->value[x]]) + 6, "%d", x + 1);
                    }
                } else {
                    printLines(miniwin, translated[1], 1, 1, 1);
                }
                n_entries = entries->end;

                break;
            case 1:
                if (targets->subcount < 1) {
                    printLines(miniwin, translated[1], 1, 1, 3);
                    n_entries = 0;
                } else {
                    for (int x = 0; x < 2; x++) {
                        printLines(miniwin, translated[1], x + 1, 1, 11 + x);
                    }
                    n_entries = 2;
                }
                break;
            case 2:
                for (int x = 0; x < (int)lang->count; x++) {
                    mvwprintw(miniwin, x + 1, 1, lang->content[x].name);
                }   
                break;
            }
            update = false;
        }
        
        //Draw Cursor
        switch (cursor[2])
        {
        case 0:
            mvwchgat(sidebar, cursor[1] + 1, 1, optLenght, A_STANDOUT, 0, NULL);
            break;
        case 1:
            switch (cursor[1])
            {
            case 0:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(entries->item[cursor[0]]) + 4, A_STANDOUT, 0, NULL);
                break;
            case 1:
                mvwchgat(miniwin, cursor[0]+1, 1, optLenght + 1, A_STANDOUT, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name) + 1, A_STANDOUT, 0, NULL);
                break;
            }
            break;
        case 2:
            if (type == 0) {
                if (cursor[0] == 0) {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/4), actionLenght[0], A_STANDOUT, 0, NULL);
                } else{
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])*3/4), actionLenght[1], A_STANDOUT, 0, NULL);
                }
            } else {
                mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/2), actionLenght[0], A_STANDOUT, 0, NULL);
            }

            break;
        }

        wrefresh(miniwin);
        wrefresh(sidebar);
        wrefresh(action);
        
        input = wgetch(window);

        //Erase Cursor
        switch (cursor[2])
        {
        case 0:
            mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_NORMAL, 0, NULL);
            break;
        case 1:
            switch (cursor[1])
            {
            case 0:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(entries->item[cursor[0]]) + 4, A_NORMAL, 0, NULL);
                break;
            case 1:
                mvwchgat(miniwin, cursor[0]+1, 1, optLenght + 1, A_NORMAL, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name) + 1, A_NORMAL, 0, NULL);
                break;
            }
            break;
        case 2:
            if (type == 0) {
                if (cursor[0] == 0) {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/4), actionLenght[0], A_NORMAL, 0, NULL);
                } else{
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])*3/4), actionLenght[1], A_NORMAL, 0, NULL);
                }
            } else {
                mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/2), actionLenght[0], A_NORMAL, 0, NULL);
            }
            break;
        }

        switch (input)
        {
        case KEY_DOWN:
            if (cursor[2] == 0 && cursor[1] < 3) {
                cursor[0] = 0;
                cursor[1]++;
                update = true;
            } else if (cursor[2] == 1 && cursor[0] < n_entries-1) {
                cursor[0]++;
            }
            break;
        case KEY_UP:
            if (cursor[2] == 0 && cursor[1] > 0) {
                cursor[0] = 0;
                cursor[1]--;
                update = true;
            } else if (cursor[2] == 1 && cursor[0] > 0) {
                cursor[0]--;
            }
            break;
        case KEY_LEFT:
            if (cursor[2] == 2) {
                cursor[0] = !cursor[0];
            }
            break;
        case KEY_RIGHT:
            if (cursor[2] == 2) {
                cursor[0] = !cursor[0];
            }
            break;
        case ENTER:
            // Case focused on the sidebar, else subwin or else action panel
            if (cursor[2] == 0) {
                switch (cursor[1])
                {
                case 0:
                    if (targets->subcount > 0) {
                        type = 1;
                        relay_message = 10;
                        wclear(action);
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        cursor[0] = 0;
                        cursor[2] = 2;
                    } else if (query->end > 0) {
                        type = 0;
                        relay_message = 5;
                        wclear(action);
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        cursor[0] = 0;
                        cursor[2] = 2;
                    }
                    break;
                case 1:
                    type = 0;
                    relay_message = 11;
                    wclear(action);
                    confirmationDialog(translated[1], relay_message, actionLenght, type);
                    cursor[0] = 0;
                    cursor[2] = 2;
                    break;
                case 3:
                    quit = true;
                    break;
                }
            } else if (cursor[2] == 1) {
                switch (cursor[1])
                {
                case 0:
                    if (entries->value[cursor[0]] == 1) {
                        for (int x = 0; x < query->end; x++) {
                            if (strcmp(entries->item[cursor[0]], query->item[x]) == 0) {
                                deQueue(query, x);
                                break;
                            }
                        }
                    } else if (entries->value[cursor[0]] == 0) {
                        enQueue(query, entries->item[cursor[0]]);
                        query->value[query->end-1] = cursor[0];
                    }
                    entries->value[cursor[0]] = !entries->value[cursor[0]];

                    mvwprintw(sidebar, 1, optLenght + 1, "%s", (query->end > 0) ? "[!]" : "");

                    for (int x = 0; x < entries->end; x++) {
                        mvwprintw(miniwin, 1+x, strlen(entries->item[x]) + 2, "[%c]  ", (entries->value[x] == 1) ? 'X' : ' ');
                    }
                    for (int x = 0; x < query->end; x++) {
                        mvwprintw(miniwin, 1 + query->value[x], strlen(entries->item[x]) + 6, "%d", x + 1);
                    }
                        
                    break;
                case 1:
                    /* code */
                    break;
                case 2:
                    for (int x = 0; x < 3; x++) {
                        free(translated[x]);
                    }
                    free(translated);
                    translated = getLang(lang, cursor[0]);
                    updateWindows();
                    optLenght = printLines(NULL, translated[0], 0, 1, 0);

                    update = true;
                    break;
                default:
                    break;
                }
            } else if (cursor[2] == 2) {
                //Case the confirmation dialog is triggered, switch to different tabs actions
                switch (cursor[1]) {
                case 0:
                    wclear(action);
                    if (cursor[0] == 0 && targets->subcount < 1) {
                        type = 1;
                        for (int x = 0; x < query->end; x++) {
                            targets->subdir[x] = *scanFolder(targets, path, query->value[x]);
                        }
                        
                        relay_message = 6;

                        confirmationDialog(translated[1], relay_message, actionLenght, 1);
                        endQueue(query);
                        query = initQueue(8);
                    } else {
                        cursor[2] = 0;
                        update = true;
                    }
                    break;
                case 1:
                    if (cursor[0] == 0 && targets->subcount > 0) {
                        for (int x = 0; x < (int)targets->subcount; x++) {
                            overrideFiles(&targets->subdir[0], &targets->subdir[1]);
                        }
                    }
                    break;
                }
            }
            break;
        case TAB:
            if (cursor[2] != 2) {
                switch (cursor[1])
                {
                case 0:
                    if (targets->subcount < 1 && entries->end > 0) {
                        cursor[2] = !cursor[2];
                    }
                    break;
                case 1:
                    if (targets->subcount > 0) {
                        cursor[2] = !cursor[2];
                    }
                    break;
                case 2:
                    if (lang->count > 0) {
                        cursor[2] = !cursor[2];
                    }
                    break;
                }
            }
            break;
        case KEY_RESIZE:
            updateWindows();
            update = true;

            if (cursor[2] == 2) {
                confirmationDialog(translated[1], relay_message, actionLenght, type);
            }
            
            break;
        default:
            break;
        }
    }

    logger("Ending program, logging.\n");
    returnString(&path, "path");
    printLog(path);
    
    //Free query
    for (int x = 0; x < 3; x++) {
        free(translated[x]);
    }
    free(_sidebar);
    free(_window);
    free(_miniwin);
    free(_action);
    free(translated);
    free(report);
    free(path);
    delwin(window);
    delwin(sidebar);
    delwin(miniwin);
    delwin(action);
    endQueue(query);
    endQueue(entries);
    freeFolder(targets);
    freeFolder(lang);
    endwin();

    return 0;
}