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

#define ENTER 10
#define TAB '\t'
#define FOLDERCHUNK 16
#define FILECHUNK 64

int n_folders = 0;
int n_files = 0;
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

    //"(8), (36), ((LINES - 8) / 2), ((COLS - 36) / 2)"
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

//Read from a target file into the memory
//pack is the parent folder, path is the path to loop for and position is the file position. -1 Will take the path as target.
FOLDER* scanFolder(FOLDER* pack, char* path, int position) {
    int dirNumber = 0, result = 0, type = 0, folderCursor = FOLDERCHUNK;
    long *dirPosition = (long*)calloc(folderCursor, sizeof(long));
    struct dirent *entry;
    char placeholder[1024], *location = strdup(path);

    if (dirPosition == NULL) {
        perror("Error allocating memory for directory cursor");
        free(dirPosition);
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
    } else {
        printf("Error acessing %s: %s\n", location, strerror(errno));
        return NULL;
    }
    FOLDER* folder = createFolder(pack, placeholder);

    //Differentiate folder from zip file
    type = fileType(location);
    if ((type) == 0) {

        closedir(scanner);
        scanner = opendir(location);
        if (scanner != NULL) {
            seekdir(scanner, 2);
        } else {
            printf("Could not open %s: %s\n", location, strerror(errno));
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
                    returnString(&location, "path");
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
                    printf("scanFolder: error openning %s %s\n", location, strerror(errno));
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
                
                printf("FOLDER: <%s>\n", location);

            } else if (type == 1) {
                ARCHIVE* pointer = getFile(location);
                addFile(&folder, pointer);
                printf("FILE: <%s>\n", location);

            }
            returnString(&location, "path");
            
        }
        free(location);
        return folder;
        
    } else if (type == 2) {
        closedir(scanner);
        unzFile rp = unzOpen(location);
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

        free(location);
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
    } else {
        printf("<%s>", path);
        perror("Invalid path");
        return 1;
    }

    //Scanning the lang folder
    returnString(&path, "lang");
    lang = scanFolder(NULL, path, -1);
    returnString(&path, "path");

    FOLDER* targets = createFolder(NULL, "targets");
    targets->subdir = (FOLDER*)calloc(2, sizeof(FOLDER));

    //Getting lang file
    char** translated = getLang(lang, 0);
        
    //Starting the menu;
    initscr();
    int input = 0, cursor[3], optLenght, actionLenght[2], type = 0, relay_message = -1;
    cursor[0] = cursor[1] = cursor[2] = 0;
    bool quit = false, update = false;
    n_entries = 0;

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

    char* temp = strchr(translated[2], '\n');
    size_t center = temp - translated[2];
    center = (COLS*3/4) - center;
    center /= 2;

    box(sidebar, 0, 0);
    optLenght = (int)printLines(sidebar, translated[0], 0, 1, 0);
    printLines(window, translated[2], 0, center, 0);
    wrefresh(sidebar);
    wrefresh(window);

    query = initQueue(8);
    entries = initQueue(8);

    while (!quit) {
        
        if (cursor[2] == 0 || update == true) {
            wclear(miniwin);
            box(miniwin, 0, 0);
            switch (cursor[1])
            {
            case 0:
                if (entries->end < 1) {
                    returnString(&path, "RP1");
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
                    printLines(miniwin, translated[1], 0, 1, 2);
                    for (int x = 0; x < entries->end; x++) {
                        if (entries->value[x] == 0) {
                            mvwprintw(miniwin, x+1, 1, "%s [ ]  ", entries->item[x]);
                        } else {
                            mvwprintw(miniwin, x+1, 1, "%s [X]  ", entries->item[x]);
                        }
                    }
                    for (int x = 0; x < query->end; x++) {
                        mvwprintw(miniwin, query->value[x]+1, strlen(entries->item[query->value[x]])+6, "%d", x+1);
                    }
                } else {
                    printLines(miniwin, translated[1], 1, 1, 1);
                }
                n_entries = entries->end;
                break;
            case 1:
                if (targets->subcount > 0) {
                    for (int x = 0; x < (int)targets->subcount; x++) {
                        mvwprintw(miniwin, x+1, 1, targets->subdir[x].name);
                    }
                } else {
                    printLines(miniwin, translated[1], 1, 1, 3);
                }
                n_entries = targets->subcount;
                break;
            case 2:
                for (int x = 0; x < (int)lang->count; x++) {
                    mvwprintw(miniwin, x+1, 1, lang->content[x].name);
                }
                n_entries = lang->count;
                break;
            }

            if (query->end > 0) {
                mvwprintw(sidebar, 1, optLenght+1, "[!]");
            }
        }
        
        if (cursor[2] == 0) {
            mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_STANDOUT, 0, NULL);
        } else if (cursor[2] == 1) {
            switch (cursor[1])
            {
            case 0:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(entries->item[cursor[0]])+4, A_STANDOUT, 0, NULL);
                break;
            case 1:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(targets->subdir[cursor[0]].name)+1, A_STANDOUT, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name)+1, A_STANDOUT, 0, NULL);
                break;
            }
        } else if (cursor[2] == 2) {
            if (cursor[0] == 0 && type == 0) {
                mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/4), actionLenght[0], A_STANDOUT, 0, NULL);
            } else if (type == 0){
                mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])*3/4), actionLenght[1], A_STANDOUT, 0, NULL);
            } else if (type == 1) {
                mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])/2), actionLenght[0], A_STANDOUT, 0, NULL);
            }
            wrefresh(action);
        }
        update = false;
        
        wrefresh(miniwin);
        wrefresh(sidebar);
        
        input = wgetch(window);

        if (cursor[2] == 1) {
            switch (cursor[1])
            {
            case 0:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(entries->item[cursor[0]])+4, A_NORMAL, 0, NULL);
                break;
            case 1:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(targets->subdir[cursor[0]].name)+1, A_NORMAL, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name)+1, A_NORMAL, 0, NULL);
                break;
            }
        } else if (cursor[2] == 0) {
            mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_NORMAL, 0, NULL);
        }

        switch (input)
        {
        case KEY_DOWN:
            if (cursor[2] == 0 && cursor[1] < 3) {
                cursor[0] = 0;
                cursor[1]++;
            } else if (cursor[2] == 1 && cursor[0] < n_entries-1) {
                cursor[0]++;
            }
            break;
        case KEY_UP:
            if (cursor[2] == 0 && cursor[1] > 0) {
                cursor[0] = 0;
                cursor[1]--;
            } else  if (cursor[2] == 1 && cursor[0] > 0) {
                cursor[0]--;
            }
            break;
        case KEY_LEFT:
            if (cursor[2] == 2) {
                cursor[0] = !cursor[0];
                if (cursor[0] == 0 && type == 0) {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])*3/4), actionLenght[1], A_NORMAL, 0, NULL);
                } else if (type == 0) {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/4), actionLenght[0], A_NORMAL, 0, NULL);
                }
            }
            break;
        case KEY_RIGHT:
            if (cursor[2] == 2) {
                cursor[0] = !cursor[0];
                if (cursor[0] == 0) {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[1])*3/4), actionLenght[1], A_NORMAL, 0, NULL);
                } else {
                    mvwchgat(action, (getmaxy(action) - 2), ((getmaxx(action) - actionLenght[0])/4), actionLenght[0], A_NORMAL, 0, NULL);
                }
            }
            break;
        case ENTER:
            // Case focused on the sidebar, else subwin or else action panel
            if (cursor[2] == 0) {
                switch (cursor[1])
                {
                case 0:
                    if (query->end > 0) {
                        type = 0;
                        relay_message = 5;
                        wclear(action);
                        confirmationDialog(translated[1], relay_message, actionLenght, 0);
                        cursor[0] = 0;
                        cursor[2] = 2;
                        wrefresh(action);
                    }
                    break;
                case 1:
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

                    for (int x = 0; x < entries->end; x++) {
                        if (entries->value[x] == 0) {
                            mvwprintw(miniwin, x+1, 1, "%s [ ]  ", entries->item[x]);
                        } else {
                            mvwprintw(miniwin, x+1, 1, "%s [X]  ", entries->item[x]);
                        }
                    }

                    for (int x = 0; x < query->end; x++) {
                        mvwprintw(miniwin, query->value[x]+1, strlen(entries->item[query->value[x]])+6, "%d", x+1);
                    }

                    if (query->end > 0) {
                        mvwprintw(sidebar, 1, optLenght+1, "[!]");
                    } else {
                        mvwprintw(sidebar, 1, optLenght+1, "   ");
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
                    wclear(sidebar);
                    wclear(window);
                    wclear(miniwin);
                    box(sidebar, 0, 0);
                    box(miniwin, 0, 0);
                    optLenght = printLines(sidebar, translated[0], 0, 1, 0);

                    temp = strchr(translated[2], '\n');
                    center = temp - translated[2];
                    center = (COLS*3/4) - center;
                    center /= 2;

                    printLines(window, translated[2], 0, center, 0);
                    wrefresh(sidebar);
                    wrefresh(window);
                    wrefresh(miniwin);
                    update = true;
                    break;
                default:
                    break;
                }
            } else if (cursor[2] == 2) {
                wclear(action);
                //Case the confirmation dialog is triggered, switch to different tabs actions
                switch (cursor[1]) {
                case 0:
                    if (cursor[0] == 0 && targets->subcount < 1) {
                        type = 1;
                        for (int x = 0; x< query->end; x++) {
                            targets->subdir[x] = *scanFolder(targets, path, query->value[x]);
                        }
                        
                        relay_message = 6;
                        confirmationDialog(translated[1], relay_message, actionLenght, 1);
                        endQueue(query);
                        query = initQueue(8);
                    } else if (cursor[0] == 0) {
                        cursor[2] = 0;
                        update = true;

                    }
                    break;
                case 1:
                    /* code */
                    break;
                }
            }
            break;
        case TAB:
            if (cursor[2] != 2 && cursor[1] == 0 && entries->end > 0) {
                cursor[2] = !cursor[2];
                mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_NORMAL, 0, NULL);
            } else if (cursor[2] != 2 && cursor[1] == 1 && targets->subcount > 0) {
                cursor[2] = !cursor[2];
                mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_NORMAL, 0, NULL);
            } else if (cursor[2] != 2 && cursor[1] == 2 && lang->count > 0) {
                cursor[2] = !cursor[2];
                mvwchgat(sidebar, cursor[1]+1, 1, optLenght, A_NORMAL, 0, NULL);
            }
            break;
        case KEY_RESIZE:
            resize_term(0, 0);
            endwin();
            refresh();
            clear();

            wclear(window);
            wclear(sidebar);
            wclear(miniwin);
            wclear(action);

            calcWindow(_window);
            calcWindow(_sidebar);
            calcWindow(_miniwin);
            calcWindow(_action);

            resize_window(window, _window->size_y, _window->size_x);
            resize_window(sidebar, _sidebar->size_y, _sidebar->size_x);
            resize_window(miniwin, _miniwin->size_y, _miniwin->size_x);
            resize_window(action, _action->size_y, _action->size_x);

            mvwin(action, _action->y, _action->x);
            confirmationDialog(translated[1], relay_message, actionLenght, type);

            box(miniwin, 0, 0);
            box(sidebar, 0, 0);
            optLenght = (int)printLines(sidebar, translated[0], 0, 1, 0);
            temp = strchr(translated[2], '\n');
            center = temp - translated[2];
            center = (COLS-32) - center;
            center /= 2;
            printLines(window, translated[2], 0, center, 0);

            if (cursor[0] == 0) {
                for (int x = 0; x < entries->end; x++) {
                    if (entries->value[x] == 0) {
                        mvwprintw(miniwin, x+2, 1, "%s [ ]  ", entries->item[x]);
                    } else {
                        mvwprintw(miniwin, x+2, 1, "%s [X]  ", entries->item[x]);
                    }
                }
                for (int x = 0; x < query->end; x++) {
                    mvwprintw(miniwin, query->value[x]+1, strlen(entries->item[query->value[x]])+6, "%d", x+1);
                }
                printLines(miniwin, translated[1], 0, 1, 2);
            }

            wrefresh(sidebar);
            wrefresh(window);
            wrefresh(miniwin);

            break;
        default:
            break;
        }
    }

    //Free query
    for (int x = 0; x < 3; x++) {
        free(translated[x]);
    }
    free(_sidebar);
    free(_window);
    free(_miniwin);
    free(_action);
    free(translated);
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