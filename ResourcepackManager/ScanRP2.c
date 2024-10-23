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
#include <locale.h>

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
    ARCHIVE* content;
} FOLDER;

typedef struct RESOLUTION {
    WINDOW* window;
    int parameters[4][7];
    int y, x, size_y, size_x;
} RESOLUTION;

typedef struct OBJECT {
    char* declaration;
    struct OBJECT* value;
    struct OBJECT* parent;
    size_t count;
    size_t capacity;
} OBJECT;

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
    queue->item = (char**)calloc(size, sizeof(char*));

    for (int x = 0; x < (int)size; x++) {
        queue->item[x] = NULL;
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
    queue->item[queue->end] = strdup(item);
        queue->end++;
}

void peekQueue(WINDOW* window, int y, int x, QUEUE* queue) {
    for (int i = 0; i < queue->end; i++) {
        mvwprintw(window, y+i, x, "%s [%d]", queue->item[i], queue->value[i]);
    }
}

void deQueue(QUEUE* queue, int item) {
    free(queue->item[item]);
    for (int x = item; x < queue->end - 1; x++) {
        queue->item[x] = queue->item[x+1];
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

char* returnPath (FOLDER* folder) {
    QUEUE* list = initQueue(16);
    char* path, *temp;
    size_t length = 0, capacity = 1024;

    path = (char*)calloc(capacity, sizeof(char));

    while (strcmp(folder->parent->name, "assets") != 0 && folder->parent != NULL) {
        enQueue(list, folder->name);
        folder = folder->parent;
    }

    for (size_t x = list->end; x > 0; x--) {
        if (length + strlen(list->item[x - 1]) >= capacity) {
            capacity *= 2;

            temp = realloc(path, capacity * sizeof(char));
            if (temp != NULL) {
                path = temp;
                temp = NULL;
            } else {
                logger("Failed to reallocate path string when concatenating the path\n");
                return NULL;
            }
        }

        if (x == 0) {
            strcat(path, list->item[x]);
        } else {
            sprintf(path + strlen(path), "%s/", list->item[x - 1]);
        }
    }

    temp = realloc(path, (strlen(path) + 1) * sizeof(char));
    if (temp != NULL) {
        path = temp;
        temp = NULL;
    } else {
        logger("Failed to reallocate path string when trimming the path\n");
    }

    return path;
}

void printLog(char* path) {
    char date[1024];

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    sprintf(date, "%s\\log\\log_%02d-%02d_%02d-%02d.txt", path, tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, tm.tm_min);
    
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

OBJECT* createOBJ (char* key) {
    OBJECT* file = (OBJECT*)malloc(sizeof(OBJECT));
    file->capacity = 1;
    file->count = 0;
    file->declaration = strdup(key);
    file->parent = NULL;

    file->value = (OBJECT*)malloc(sizeof(OBJECT));
    return file;
}

void addOBJ (OBJECT** file, OBJECT* value) {
    if (file[0]->count == file[0]->capacity) {
        file[0]->capacity *= 2;

        OBJECT* temp = (OBJECT*)realloc(file[0]->value, file[0]->capacity * sizeof(OBJECT));
        if (temp == NULL) {
            logger("Error reallocating memory for %s\n", file[0]->declaration);
            return;
        } else {
            logger("Reallocated memory for %s sucessfully\n", file[0]->declaration);
            file[0]->value = temp;
        }
    }

    value->parent = file[0];
    file[0]->value[file[0]->count] = *value;
    file[0]->count++;
}

void delOBJ (OBJECT** file, size_t key) {
    OBJECT* compass = &file[0]->value[key];
    free(file[0]->value[key].declaration);

    for (; compass->count > 0; compass->count--) {
        delOBJ(&compass, compass->count);
    }
    free(file[0]->value[key].value);

    for (size_t x = key; x < (file[0]->count - 1); x++) {
        file[0]->value[x] = file[0]->value[x + 1];
    }

    file[0]->count--;
    if (file[0]->count == file[0]->capacity/2 && file[0]->capacity/2 > 0) {
        file[0]->capacity /= 2;

        OBJECT* temp = (OBJECT*)realloc(file[0]->value, file[0]->capacity * sizeof(OBJECT));
        if (temp == NULL) {
            logger("Shrank %s due to file removal\n", temp->declaration);
            file[0]->value = temp;
        } else {
            logger("Failed to shrink %s!\n", file[0]->declaration);
        }
    }
}

OBJECT* dupOBJ (OBJECT* target) {
    OBJECT* mirror = createOBJ(target->declaration);
    OBJECT* temp;
    for (int x = 0; x < (int) target->count; x++) {
        temp = dupOBJ(&mirror->value[x]);
        addOBJ(&mirror, temp);
        temp = NULL;
    }

    return mirror;
}

void freeOBJ (OBJECT* file) {
    if (file == NULL) {
        return;
    }

    while (file[0].count > 0) {
        freeOBJ(&file[0].value[file[0].count - 1]);
        file[0].count--;
    }

    if (file[0].declaration != NULL) {
        free(file[0].declaration);
    }
    if (file[0].value != NULL) {
        free(file[0].value);
    }
}

char* strnotchr(const char* str, int count, ...) {
    char* chars = (char*)calloc(count, sizeof(char));
    va_list list;
    va_start(list, count);
    bool found = false;

    for (int x = 0; x < count; x++) {
        chars[x] = va_arg(list, int);
    }

    while (*str != '\0') {
        for (int x = 0, y = 0; x < count && y == 0; x++) {
            if (*str == chars[x]) {
                y++;
            }
            if (x == count - 1 && y == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        str++;
    }

    free(chars);
    va_end(list);
    if (found) {
        return (char*)str;
    } else {
        return NULL;
    }
}

char* strchrs (const char* str, int count, ...) {
    char* chars = (char*)calloc(count, sizeof(char));
    va_list list;
    va_start(list, count);
    bool found = false;

    for (int x = 0; x < count; x++) {
        chars[x] = va_arg(list, int);
    }

    while (*str != '\0') {
        for (int x = 0, y = 0; x < count && y == 0; x++) {
            if (*str == chars[x]) {
                y++;
            }
            if (x == count - 1 && y == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
        str++;
    }

    free(chars);
    va_end(list);
    if (found) {
        return (char*)str;
    } else {
        return NULL;
    }
}

OBJECT* processOBJ (char* obj) {
    char *pointer, *checkpoint, buffer[512];
    OBJECT *compass, *entry;
    size_t length;
    bool array = false;

    //An obj contains key-value. The value *can* be another object
    //An array can contain objects

    //continue debugging. The value scan jump fixed, now it's leaving to parent key after finished current object
    compass = createOBJ("obj");
    for (int x = 1; x < (int)strlen(obj); x++) {
        switch (obj[x])
        {
        case '{':
            entry = createOBJ("obj");
            addOBJ(&compass, entry);
            entry = NULL;
            compass = &compass->value[compass->count - 1];
            break;
        case '}':
            break;
        case '\"':
            pointer = strchr(obj + x + 1, '\"');
            pointer++;

            length = (pointer - (obj + x));
            sprintf(buffer, "%.*s", (int)length, obj + x);
            entry = createOBJ(buffer);
            addOBJ(&compass, entry);
            entry = NULL;

            x += length - 1;
            break;
        case ':':
            compass = &compass->value[compass->count - 1];
            x += 2;
            array = false;

            while (*pointer != ']') {
                pointer = strnotchr((obj + x), 3, ' ', '\n', '\t');

                switch (*pointer)
                {
                case '[':
                    entry = createOBJ("array");
                    addOBJ(&compass, entry);
                    entry = NULL;
                    compass = &compass->value[compass->count - 1];

                    x++;
                    array = true;

                    break;
                case '{':
                    for (int alpha = 1, beta = 1; alpha < strlen(obj); alpha++) {
                        if (pointer[alpha] == '{') {
                            beta++;
                        } else if (pointer[alpha] == '}') {
                            beta--;
                        }
                        if (beta == 0) {
                            checkpoint = &pointer[alpha + 1];
                            x = (checkpoint - obj);
                            break;
                        }
                    }

                    length = (checkpoint - pointer);
                    checkpoint = NULL;
                    checkpoint = (char*)calloc(length + 1, sizeof(char));
                    sprintf(checkpoint, "%.*s", (int)length, pointer);

                    entry = processOBJ(checkpoint);
                    addOBJ(&compass, entry);
                    entry = NULL;
                    free(checkpoint);
                    checkpoint = NULL;

                    break;
                case ']':
                case '}':
                    x = (pointer - obj);
                    array = false;

                    break;
                default:
                    x = (pointer - obj);

                    if (*pointer == '\"') {
                        pointer = strchr(obj + x + 1, '\"');
                        pointer++;
                    } else {
                        pointer = strchrs(pointer, 5, ' ', ',', '\n', '\t', ']');
                    }

                    length = (pointer - (obj + x));
                    sprintf(buffer, "%.*s", (int)length, obj + x);

                    entry = createOBJ(buffer);
                    addOBJ(&compass, entry);
                    entry = NULL;

                    x += length + 1;

                    break;
                }
                // Failing at nested array in the nested object, leaving to parent when shouldn't and copying 3 plus the closing ']', strchrs is failing somehow
                if (!array || *pointer == ']') {
                    compass = compass->parent;
                    break;
                }
            }
            break;
        }
    }
    /* 
    for (int x = 0; x < (int)strlen(obj); x++) {
        switch (obj[x])
        {
        case '{':
            entry = createOBJ("obj");
            addOBJ(&compass, entry);
            entry = NULL;
            compass = &compass->value[compass->count - 1];
            break;
        case '}':
            compass = compass->parent;
            break;
        case ']':
            compass = compass->parent;
            break;
        case '\"':
            pointer = strchr(obj + x + 1, '\"');
            pointer++;

            length = (pointer - (obj + x));
            sprintf(buffer, "%.*s", (int)length, obj + x);
            entry = createOBJ(buffer);
            addOBJ(&compass, entry);
            entry = NULL;

            x += length - 1;
            break;
        case ':':
            compass = &compass->value[compass->count - 1];
            pointer = &obj[x + 1];

            if (obj[x + 2] == '[') {
                array = true;
            }

            for (; x < (int)strlen(obj); x++) {
                pointer = strnotchr(pointer + 1, 4, ' ', '\n', '\t', '[');
                x = (pointer - obj);

                if (obj[x] == '{') {
                    x--;
                    break;
                } else if (obj[x] == '\"'){
                    pointer = strchr(pointer + 1, '\"');
                    pointer++;
                } else {
                    pointer = strchrs(pointer + 1, 4, '\n', ' ', ',', ']');
                }

                length = (pointer - (obj + x));
                sprintf(buffer, "%.*s", (int)length, obj + x);
                entry = createOBJ(buffer);
                addOBJ(&compass, entry);
                entry = NULL;

                x += length;

                if (!array || *pointer == ']') {
                    compass = compass->parent; //Functioning for the most part. I though I had organized it, but it's a mess again.
                    break;
                }
            }
            array = false;
            break;
        }
    }
    */
    return compass;
}

void addTab (char** file) {
    char *pointer, *checkpoint, *save, *placeholder;
    size_t count = 0, length;

    for (pointer = strchr(*file, '\n'); pointer != NULL; pointer = strchr(pointer + 1, '\n')) {
        count++;
    }

    placeholder = strdup(*file);

    length = strlen(*file) + count;
    save = (char*)realloc(*file, (length + 1) * sizeof(char));

    if (save != NULL) {
        *file = save;
    } else {
        return;
    }

    *file[0] = '\0';

    for (
        pointer = checkpoint = placeholder, pointer = strchr(pointer + 1, '\n');
        pointer != NULL;
        checkpoint = pointer, pointer = strchr(pointer + 1, '\n')
    ) {
        pointer++;
        sprintf(*file + strlen(*file), "%.*s\t", (int)(pointer - checkpoint), checkpoint);
    }

    strcat(*file, "}");
    free(placeholder);
}

char* printJSON (OBJECT* json) {
    char *pointer, *checkpoint, *temp, *buffer = NULL, *placeholder = NULL, *backup, *obj = "{\n\t%s}", *array = "[\n\t%s]";
    OBJECT *navigator;
    size_t length, size = 0;

    for (size_t x = 0; x < json->count; x++) {
        navigator = &json->value[x];

        if (strcmp(navigator->value->declaration, "obj") == 0) {

            if (navigator->count > 1) {
                length = 0;
                for (size_t y = 0; y < navigator->count; y++) {
                    pointer = printJSON(&navigator->value[y]);
                    addTab(&pointer);

                    size = snprintf(NULL, 0, "%s%s", pointer, (y == navigator->count - 1) ? "\n\t" : ",\n\t");
                    temp = (char*)realloc(placeholder, (size + length + 2) * sizeof(char));
                    if (temp == NULL) {
                        logger("Failed to realloc memory when expanding key string: %s\n", strerror(errno));
                        break;
                    } else {
                        placeholder = temp;
                        temp = NULL;

                        sprintf(placeholder + length, "%s%s", pointer, (y == navigator->count - 1) ? "\n\t" : ",\n\t");
                        length += size;
                        free(pointer);
                    }
                }
                size = 0;
                length = snprintf(NULL, 0, array, placeholder);
                pointer = (char*)calloc(length + 1, sizeof(char));
                sprintf(pointer, array, placeholder);
                free(placeholder);
            } else {
                pointer = printJSON(navigator->value);
            }
        } else {
            length = snprintf(NULL, 0, "\"%s\"", navigator->value->declaration);
            pointer = (char*)calloc(length + 1, sizeof(char));
            sprintf(pointer, "\"%s\"", navigator->value->declaration);
        }
        
        length = snprintf(NULL, 0, "\"%s\": %s%s", navigator->declaration, pointer, (x == json->count - 1) ? "\n" : ",\n\t");
        temp = (char*)realloc(buffer, (length + size + 1) * sizeof(char));
        
        if (temp == NULL) {
            logger("Failed to realloc memory when expanding key string: %s\n", strerror(errno));
            break;
        } else {
            buffer = temp;
            temp = NULL;
        }

        sprintf(buffer + size, "\"%s\": %s%s", navigator->declaration, pointer, (x == json->count - 1) ? "\n" : ",\n\t");
        free(pointer);
        size += length;
    }

    length = snprintf(NULL, 0, obj, buffer);
    placeholder = (char*)calloc(length + 1, sizeof(char));
    sprintf(placeholder, obj, buffer);
    free(buffer);
    return placeholder;
}

void freeFolder(FOLDER* folder) {
    for (;folder->count > 0; folder->count--) {
        ARCHIVE* file = &folder->content[folder->count-1];
        free(file->name);
        free(file->tab);
    }
    free(folder->content);
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

    subdir->parent = parent[0];
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

    model->tab[length - 1] = '\0';

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

void addFile (FOLDER** folder, ARCHIVE* model) {
    if (folder[0]->count == 0) {
        folder[0]->content = (ARCHIVE*)calloc(folder[0]->file_capacity, sizeof(ARCHIVE));
        if (folder[0]->content == NULL) {
            logger("Error allocating memory for new file, %s\n", strerror(errno));
        }
    }
    if (folder[0]->count >= (folder[0]->file_capacity - 1)) {
        folder[0]->file_capacity *= 2;
        ARCHIVE* temp = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity * sizeof(ARCHIVE));
        // folder[0]->content = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity * sizeof(ARCHIVE));

        if (temp == NULL) {
            logger("Error reallocating memory for new file, %s\n", strerror(errno));
        } else {
            folder[0]->content = temp;
        }
    }
    folder[0]->content[folder[0]->count] = *model;
    folder[0]->count++;
}

void delFile (FOLDER** folder, int target) {
    free(folder[0]->content[target].name);
    free(folder[0]->content[target].tab);
    for (int x = target; x < (int)folder[0]->count; x++) {
        folder[0]->content[x] = folder[0]->content[x + 1];
    }
    
    if (folder[0]->count == folder[0]->file_capacity/2 && folder[0]->file_capacity/2 > 0) {
        ARCHIVE* backup = (ARCHIVE*)realloc(folder[0]->content, folder[0]->capacity * sizeof(ARCHIVE));
        if (backup == NULL) {
            logger("Couldn't trim %s content size\n", folder[0]->name);
        } else {
            folder[0]->content = backup;
            folder[0]->count--;
        }
    }
}

ARCHIVE* dupFile(ARCHIVE* file) {
    ARCHIVE* dup = (ARCHIVE*)malloc(sizeof(ARCHIVE));

    dup->name = strdup(file->name);
    dup->tab = strdup(file->tab);
    dup->size = file->size;

    return dup;
}

void mergeFile(ARCHIVE* source, ARCHIVE* target) {
    char *pointer, *checkpoint, *temp, *buffer = NULL, *placeholder = NULL;
    size_t length, total;

    if ((pointer = strstr(source->tab, "\"overrides\"")) != NULL) {
        logger("%s has overrides, prepare to copy\n", target->name);
        buffer = NULL;

        checkpoint = pointer + 13;
        checkpoint = strchr(checkpoint, ']');
        length = checkpoint - pointer + 1;

        buffer = (char*)calloc(length, sizeof(char));
        if (buffer != NULL) {
            strncpy(buffer, pointer, length - 1);
        } else {
            logger("Failed to alocate memory for buffer, %s\n", strerror(errno));
        }
    }

    if ((pointer = strstr(target->tab, "\"overrides\"")) != NULL && buffer != NULL) {
        if (buffer != NULL) {
            logger("%s has overrides, prepare to insert in %s overrides\n", source->name, target->name);
            temp = buffer;
            buffer = NULL;

            //Getting the inner contents
            pointer = strchr(pointer, '{');
            checkpoint = strchr(pointer, ']');
            total = (checkpoint - pointer);
            
            for (; total > 0; total--) {
                if (pointer[total] == '}') {
                    total++;
                    break;
                }
            }

            placeholder = (char*)calloc(total, sizeof(char));
            strncpy(placeholder, pointer, total);
            placeholder[total - 1] = '\0';

            //Finding the end of the list in the first overrides
            for (int x = length - 1; x > 0; x--) {
                if (temp[x] == '}') {
                    pointer = &temp[x];
                    pointer++;
                    break;
                }
            }

            buffer = (char*)calloc(length + total + 4, sizeof(char));
            snprintf(buffer, (length + total + 4), "%.*s,\n\t\t%s%s", (int)(pointer - temp), temp, placeholder, pointer);
            free(temp);
            free(placeholder);
        } else {
            logger("%s has overrides, prepare the copy\n", target->name);

            checkpoint = pointer + 13;
            checkpoint = strchr(checkpoint, ']');
            checkpoint++;

            length = checkpoint - pointer + 1;
            buffer = (char*)calloc(length, sizeof(char));
            if (buffer != NULL) {
                strncpy(buffer, pointer, length - 1);
                buffer[length - 1] = '\0';
            } else {
                logger("Failed to alocate memory for buffer, %s\n", strerror(errno));
            }
        }
    }

    if (buffer != NULL) {
        temp = NULL;

        if ((pointer = strstr(target->tab, "\"overrides\"")) != NULL) {
            checkpoint = pointer + 13;
            checkpoint = strchr(checkpoint, ']');
            checkpoint++;
        } else {
            pointer = strrchr(target->tab, '}');

            for (int x = (pointer - target->tab) - 1; x > 0; x--) {
                if (target->tab[x] == '}' || target->tab[x] == ']') {
                    pointer = &target->tab[x];
                    checkpoint = pointer + 1;
                    break;
                }
            }
        }

        length = (pointer - target->tab);
        length += (target->size - (checkpoint - target->tab));
        length += strlen(buffer);
        temp = (char*)calloc(length, sizeof(char));
        snprintf(
            temp,
            length,
            "%.*s%s%s",
            (int)(pointer - target->tab),
            target->tab,
            buffer,
            checkpoint
        );

        free(buffer);
        free(target->tab);
        target->tab = temp;
        target->size = length;
    } else {
        free(target->tab);
        free(target->name);

        target->tab = strdup(source->tab);
        target->name = strdup(source->name);
        target->size = source->size;
    }
}

FOLDER* dupFolder (FOLDER* base) {
    FOLDER* dup = createFolder(NULL, base->name);

    dup->capacity = base->capacity;
    dup->file_capacity = base->file_capacity;
    dup->content = (ARCHIVE*)calloc(dup->file_capacity, sizeof(ARCHIVE));
    dup->subdir = (FOLDER*)calloc(dup->capacity, sizeof(FOLDER));

    for (int x = 0; x < (int)base->count; x++) {
        ARCHIVE* mirror = dupFile(&base->content[x]);
        logger("- %s\n", base->content[x].name);
        addFile(&dup, mirror);
        mirror = NULL;
    }

    for (int x = 0; x < (int)base->subcount; x++) {
        FOLDER* mirror = dupFolder(&base->subdir[x]);
        logger("- /%s\n", base->subdir[x].name);
        addFolder(&dup, mirror);
        mirror = NULL;
    }

    return dup;
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
size_t mvwprintLines(WINDOW* window, char* list, int y, int x, int from, int to) {
    size_t lenght, big = 0;
    char *pointer = list, *checkpoint = list;

    if (to == -1) {
        for (int x = 1; x < from; x++) {
            pointer = strchr(pointer, '\n');
            pointer++;
        }
        
        while ((pointer = strchr(pointer, '\n')) != NULL) {
            pointer++;
            lenght = (pointer - checkpoint);
            mvwprintw(window, y, x, "%.*s", lenght-1, checkpoint);

            y++;
            checkpoint = pointer;

            if (big < lenght) {
                big = lenght;
            }

        }
    } else {
        for (int x = 1; x < from; x++) {
            pointer = strchr(pointer, '\n');
            pointer++;
        }

        for (int z = from - 1; z < to; z++, y++) {
            checkpoint = pointer;
            pointer = strchr(pointer, '\n');
            pointer++;

            lenght = (pointer - checkpoint);
            mvwprintw(window, y, x, "%.*s", lenght - 1, checkpoint);

            if (lenght > big) {
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

    mvwprintLines(sidebar, translated[0], 0, 1, 0, -1);
    mvwprintLines(window, translated[2], 0, result, 0, -1);

    wrefresh(action);
    wrefresh(miniwin);
    wrefresh(window);
    wrefresh(sidebar);
}

//Read from a target file into the memory
//pack is the parent folder, path is the path to loop for and position is the file position. -1 Will take the path as target.
FOLDER* scanFolder(char* path, int position) {
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
    FOLDER* folder = createFolder(NULL, placeholder);

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
                mvwprintLines(miniwin, report, 1, 1, (report_end - line_number), report_end);
            } else {
                mvwprintLines(miniwin, report, 1, 1, (report_end - (getmaxy(miniwin) - 2)), report_end);
            }
            /* 
            if (line_number < (getmaxy(miniwin) - 2)) {
                for (int x = report_end - line_number, y = 1; x < (int)report_end && x > -1; x++, y++) {
                    mvwprintLines(miniwin, report, y, 1, (x + 1));
                }
            } else {
                for (int x = report_end - (getmaxy(miniwin) - 2), y = 1; x < (int)report_end && x > -1 && y < (getmaxy(miniwin) - 1); x++, y++) {
                    mvwprintLines(miniwin, report, y, 1, (x + 1));
                }
            }
            */
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
        logger("%s File Count: %d\n%s Folders Count: %d\n", name, file_count, name, folder_count);
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
                mvwprintLines(miniwin, report, 1, 1, (report_end - line_number), report_end);
            } else {
                mvwprintLines(miniwin, report, 1, 1, (report_end - (getmaxy(miniwin) - 2)), report_end);
            }
            /* 
            if (line_number < (getmaxy(miniwin) - 2)) {
                for (int x = report_end - line_number, y = 1; x < (int)report_end && x > -1; x++, y++) {
                    mvwprintLines(miniwin, report, y, 1, (x + 1));
                }
            } else {
                for (int x = report_end - (getmaxy(miniwin) - 2), y = 1; x < (int)report_end && x > -1 && y < (getmaxy(miniwin) - 1); x++, y++) {
                    mvwprintLines(miniwin, report, y, 1, (x + 1));
                }
            }
            */

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


        free(location);
        logger("%s File Count: %d\n%s Folders Count: %d\n", name, file_count, name, folder_count);
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

FOLDER* localizeFolder(FOLDER* folder, char* path, bool recreate_path) {
    char namespace[512], dir[512], buffer[256], *pointer = NULL, *checkpoint = NULL;
    FOLDER* navigator = folder;

    memset(namespace, '0', sizeof(namespace));
    memset(dir, '0', sizeof(dir));
    sscanf(path, "%512[^:]:%512[^\"]", namespace, dir);
    if (strchr(path, ':') == NULL) {
        // strcpy(dir, namespace);
        // strcpy(namespace, "minecraft");
        return folder;
    }

    for (int x = 0; x < (int)navigator->subcount + 1; x++) {
        if (x == (int)navigator->subcount) {
            FOLDER* dummy = createFolder(NULL, "assets");
            addFolder(&navigator, dummy);
            dummy = NULL;
            x = -1;

            logger("Created folder \"assets\" originaly missing\n");
        } else if (strcmp(navigator->subdir[x].name, "assets") == 0) {
            navigator = &navigator->subdir[x];
            break;
        }
    }

    for (int x = 0; x < (int)navigator->subcount + 1; x++) {
        if (x == (int)navigator->subcount) {
            FOLDER* dummy = createFolder(NULL, namespace);
            addFolder(&navigator, dummy);
            dummy = NULL;
            x = -1;

            logger("Created folder \"%s\" originaly missing\n", namespace);
        } else if (strcmp(navigator->subdir[x].name, namespace) == 0) {
            navigator = &navigator->subdir[x];
            break;
        }
    }

    for (pointer = checkpoint = dir, pointer = strchr(pointer + 1, '/'); pointer && *pointer != '\0'; pointer = strchr(pointer + 1, '/')) {
        memset(buffer, '0', sizeof(buffer));
        sprintf(buffer, "%.*s", (int)(pointer - checkpoint), checkpoint);

        for (int x = 0; x < (int)navigator->subcount + 1; x++) {
            if (x == (int)navigator->subcount && !recreate_path) {
                logger("Couldn't find %s! aborting\n", buffer);
                return NULL;
            } else if (x == (int)navigator->subcount && recreate_path) {
                FOLDER* dummy = createFolder(NULL, buffer);
                addFolder(&navigator, dummy);
                dummy = NULL;
                x = - 1;

                logger("Created folder \"%s\" originaly missing\n", buffer);
            } else if (strcmp(navigator->subdir[x].name, buffer) == 0) {
                navigator = &navigator->subdir[x];
                break;
            }
        }

        checkpoint = pointer + 1;
    }

    return navigator;
}

void overrideFiles(FOLDER* base, FOLDER* override) {
    FOLDER *navigator, *cursor;
    int position[16], coordinade[16], dirNumber = 0, line_number = -1;
    QUEUE *files;

    for (int x = 0; x < 16; x++) {
        position[x] = 0;
        coordinade[x] = 0;
    }
    nodelay(window, true);

    navigator = base;
    cursor = override;
    logger("[%s] * [%s]\n", override->name, base->name);
    line_number++;

    //Tree merging
    while (true) {
        wclear(miniwin);
        if (line_number < (getmaxy(miniwin) - 2)) {
            mvwprintLines(miniwin, report, 1, 1, (report_end - line_number), report_end);
        } else {
            mvwprintLines(miniwin, report, 1, 1, (report_end - (getmaxy(miniwin) - 2)), report_end);
        }

        if (wgetch(window) == KEY_RESIZE) {
            updateWindows();
        }
        box(miniwin, 0, 0);
        wrefresh(miniwin);

        //Entering matching folder
        for (int x = position[dirNumber]; x < (int)navigator->subcount; x++) {

            for (int y = coordinade[dirNumber]; y < (int)cursor->subcount;) {

                if (strcmp(navigator->subdir[x].name, cursor->subdir[y].name) == 0) {
                    logger("> %s\n", navigator->subdir[x].name);
                    line_number++;
                    coordinade[dirNumber] = y;
                    position[dirNumber] = x;

                    navigator = &navigator->subdir[position[dirNumber]];
                    cursor = &cursor->subdir[coordinade[dirNumber]];
                    x = 0;
                    y = 0;

                    dirNumber++;
                } else {
                    y++;
                }
            }
        }

        if (cursor->count > 0) {
            //Queuing the contents for optmization
            files = initQueue(cursor->count);
            for (int x = 0; x < (int)cursor->count; x++) {
                files->value[files->end] = x;
                enQueue(files, cursor->content[x].name);
            }
            
            for (int x = 0; x < (int)navigator->count; x++) {

                for (int y = 0; y < files->end;) {
                    
                    if (strcmp(navigator->content[x].name, files->item[y]) == 0) {
                        //Create a queue with the cursor's contents location and compare from it.
                        //Matches will be merged and the exclusives will be added.
                        logger("*%s\n", navigator->content[x].name);
                        line_number++;
                        
                        mergeFile(&cursor->content[files->value[y]], &navigator->content[x]);

                        deQueue(files, y);
                        break;
                    } else {
                        y++;
                    }
                }
            }

            //Adding the rest of the queue to the base
            if (files != NULL) {
                for (int x = 0; x < files->end; x++) {
                    ARCHIVE* mirror = dupFile(&cursor->content[files->value[x]]);
                    logger("+ %s\n", mirror->name);
                    line_number++;

                    addFile(&navigator, mirror);
                    mirror = NULL;
                }

                endQueue(files);
                files = NULL;
            }
        }

        if (dirNumber == 0) {
            break;
        }

        //Adding the exclusive folders
        if (cursor->subcount > 0) {
            //Queuing the folders for optmization
            files = initQueue(cursor->subcount);
            for (int x = 0; x < (int)cursor->subcount; x++) {
                files->value[files->end] = x;
                enQueue(files, cursor->subdir[x].name);
            }

            for (int x = 0; x < (int)navigator->subcount; x++) {
                
                for (int y = 0; y < (int)files->end;) {
                    
                    if (strcmp(navigator->subdir[x].name, files->item[y]) == 0) {
                        deQueue(files, y);
                        break;
                    } else {
                        y++;
                    }
                }
            }

            if (files != NULL) {
                for (int x = 0; x < files->end; x++) {
                    FOLDER* mirror = dupFolder(&cursor->subdir[files->value[x]]);
                    logger("++ /%s\n", mirror->name);
                    line_number++;

                    addFolder(&navigator, mirror);
                    mirror = NULL;
                }

                endQueue(files);
                files = NULL;
            }
        }

        //Leaving the node
        if (navigator->parent != NULL || cursor->parent != NULL) {
            navigator = navigator->parent;
            cursor = cursor->parent;

            position[dirNumber] = coordinade[dirNumber] = 0;

            dirNumber--;
            position[dirNumber]++;
            coordinade[dirNumber]++;

            logger("< %s\n", navigator->name);
            line_number++;
        }

    }

    logger("Finished targets linking, starting the override process\n");
}

void executeCommand(FOLDER* target, FOLDER* override) {
    char *pointer, *checkpoint, *save, *eco, backup[1024], name[256], command[256], namespace[512], buffer[2056], *path;
    path = (char*)calloc(PATH_MAX + 1, sizeof(char));
    getcwd(path, PATH_MAX);
    returnString(&path, "templates");

    FOLDER* navigator = target;
    FOLDER* cursor = override;
    FOLDER* templates = scanFolder(path, -1);
    ARCHIVE *src, *instruct, *model;
    OBJECT *pattern, *compass, *placeholder;
    int line_number = 0, char_count = 0;
    
    nodelay(window, true);
    free(path);

    for (int x = 0; x < (int)cursor->count; x++) {
        if (strstr(cursor->content[x].name, ".instruct") != NULL) {
            instruct = &cursor->content[x];
            break;
        }
    }
    if (instruct == NULL) {
        logger("Couldn't find a instruct file, make sure it's in the same folder as the mcmeta file\n");
        return;
    }

    pointer = instruct->tab;
    while ((pointer = strchr(pointer, '[')) != NULL) {
        wclear(miniwin);
        if (line_number < (getmaxy(miniwin) - 2)) {
            mvwprintLines(miniwin, report, 1, 1, (report_end - line_number), report_end);
        } else {
            mvwprintLines(miniwin, report, 1, 1, (report_end - (getmaxy(miniwin) - 2)), report_end);
        }

        if (wgetch(window) == KEY_RESIZE) {
            updateWindows();
        }
        box(miniwin, 0, 0);
        wrefresh(miniwin);

        pointer++;
        src = NULL;
        memset(buffer, '0', sizeof(buffer));
        memset(namespace, '0', sizeof(namespace));
        memset(command, '0', sizeof(command));
        memset(name, '0', sizeof(name));

        sscanf(pointer, "%2056[^[]%n", buffer, &char_count);
        buffer[char_count] = '\0';
        sscanf(buffer, "%512[^]]", namespace);

        if (strchr(command, '+') != NULL) {
            if (namespace[0] == '.') {
                logger("Source path refers to itself\n");
                cursor = localizeFolder(navigator, namespace + 2, true);
            } else {
                cursor = localizeFolder(cursor, namespace, true);
            }
        } else {
            if (namespace[0] == '.') {
                logger("Source path refers to itself\n");
                cursor = localizeFolder(navigator, namespace + 2, false);
            } else {
                cursor = localizeFolder(cursor, namespace, false);
            }
        }

        if ((checkpoint = strrchr(namespace, '/')) == NULL) {
            checkpoint = namespace;
        } else {
            checkpoint++;
        }
        sscanf(checkpoint, "%256[^]]", name);

        for (int x = 0; x < (int)cursor->count; x++) {
            if (strcmp(cursor->content[x].name, name) == 0) {
                if (command[0] == 'x') {
                    src = dupFile(&cursor->content[x]);
                    delFile(&cursor, x);
                } else {
                    src = &cursor->content[x];
                }
                break;
            }
        }

        if (src == NULL) {
            logger("Error! %s is invalid\n", namespace);
            line_number++;
            continue;
        } else {
            logger("Executing instruction for %s\n", name);
            line_number++;

            compass = processOBJ(src->tab);
            printJSON(compass);
            
        }

        checkpoint = buffer;
        checkpoint = strchr(checkpoint, '\n');
        while ((checkpoint - buffer) != (int)strlen(buffer) - 1 && checkpoint != NULL) {
            checkpoint += 5;
            memset(namespace, '0', sizeof(namespace));
            memset(command, '0', sizeof(command));
            memset(name, '0', sizeof(name));
            sscanf(checkpoint, "%256s \"%512[^\"]\"", command, namespace);

            //Command
            if (command[0] == 'x' || command[0] == 'c') {
                if (command[1] == '+') {
                    navigator = localizeFolder(navigator, namespace, true); 
                } else {
                    navigator = localizeFolder(navigator, namespace, false);
                }

                logger("Adding %s to %s", src->name, namespace);
                line_number++;

                addFile(&navigator, src);
                src = &navigator->content[navigator->count];
            } else if (strstr(command, "remove") != NULL) {
                navigator = localizeFolder(navigator, namespace, false);
                if (navigator == NULL) {
                    logger("File path is invalid\n");
                    line_number++;

                    break;
                }

                for (int x = 0; x < (int)cursor->count; x++) {
                    if (strcmp(cursor->content[x].name, src->name) == 0) {
                        delFile(&cursor, x);
                        break;
                    }
                }

            } if (strstr(command, "texture_path") != NULL) {
                //Linking textures key
                compass = processOBJ(src->tab);

                for (int x = 0; x < (int)compass->count; x++) {
                    if (strcmp(compass->value[x].declaration, "textures") == 0) {
                        placeholder = &compass->value[x];
                        break;
                    }
                }

                if (pattern == NULL) {
                    logger("\"textures\" key was not found in %s. Adding manually\n", src->name);

                    pattern = createOBJ("textures");
                    addOBJ(&compass, pattern);
                    pattern = NULL;
                    placeholder = &compass->value[compass->count];
                }
                if (strstr(command, "set") != NULL) {
                    for (int x = 0; x < (int)placeholder->count; x++) {
                        delOBJ(&placeholder, 0);
                    }
                }

                pattern = processOBJ(namespace);
                addOBJ(&placeholder->value, pattern);
                pattern = NULL;

                free(src->tab);
                src->tab = printJSON(compass);
                freeOBJ(compass);

            } if (strstr(command, "autofill") != NULL) {
                // continue debugging of atlases fill

                compass = processOBJ(src->tab);

                for (int x = 0; x < (int)compass->count; x++) {
                    if (strcmp(compass->value[x].declaration, "sources") == 0) {
                        placeholder = &compass->value[x];
                        break;
                    }
                }

                navigator = localizeFolder(navigator, "minecraft:textures/", false);
                navigator = navigator->parent->parent;

                int dirnumber = 0, position[16];
                char* temp, *scratch, *burn, *single;
                size_t length;

                // Linking model template
                for (int x = 0; x < (int)templates->count; x++) {
                    if (strcmp(templates->content[x].name, "atlases.json") == 0) {
                        model = &templates->content[x];
                        break;
                    }
                }

                save = strchr(model->tab, '[');
                save = strchr(save, '{');
                eco = save + 1;
                save = strchr(save, '}');
                save = &eco[(save - eco) - 1];

                length = (save - eco);
                scratch = (char*)calloc(length + 1, sizeof(char));
                strncpy(scratch, eco, length);

                save = strchr(save + 1, '{');
                save++;
                eco = strchr(save, '}');
                length = (eco - save);
                single = (char*)calloc(length + 1, sizeof(char));
                strncpy(single, save, length);

                for (int x = 0; x < 16; x++) {
                    position[x] = 0;
                }

                // Getting the namespaces's names
                QUEUE* namespaces = initQueue(navigator->subcount - 1);

                for (int x = 0; x < (int)navigator->subcount; x++) {
                    if (strcmp(navigator->subdir[x].name, "minecraft") != 0) {
                        enQueue(namespaces, navigator->subdir[x].name);
                    }
                }

                // Starting to travel through all namespaces
                for (int x = 0; x < namespaces->end; x++) {
                    memset(backup, '0', sizeof(backup));
                    sprintf(backup, "%s:textures/", namespaces->item[x]);

                    navigator = localizeFolder(target, backup, false);
                    memset(backup, '0', sizeof(backup));

                    while (dirnumber >= 0) {
                        while (position[dirnumber] < (int)navigator->subcount) {
                            navigator = &navigator->subdir[position[dirnumber]];

                            position[dirnumber]++;
                            dirnumber++;
                        }

                        temp = returnPath(navigator);
                        temp[strlen(temp) - 1] = '\0';

                        //scratch = dir
                        //single = single file
                        //OBJ pattern
                        //dest OBJ placeholder
                        
                        if (navigator->count > 1) {
                            length = snprintf(NULL, 0, scratch, temp, temp);

                            burn = (char*)calloc(length + 1, sizeof(char));
                            sprintf(burn, scratch, temp, temp);

                            pattern = processOBJ(burn);
                            free(burn);
                            addOBJ(&placeholder, pattern);
                            pattern = NULL;

                        } else if (navigator->count == 1) {
                            length = strlen(temp) + strlen(navigator->content->name);
                            char* moment = realloc(temp, (length + 2) * sizeof(char));
                            temp = moment;
                            moment = NULL;
                            sprintf(temp + strlen(temp), "/%s", navigator->content->name);

                            length = snprintf(NULL, 0, single, temp);
                            burn = (char*)calloc(length + 1, sizeof(char));
                            sprintf(burn, single, temp);

                            pattern = processOBJ(burn);
                            free(burn);
                            addOBJ(&placeholder, pattern);
                            pattern = NULL;
                        }
                       
                        while (position[dirnumber] == (int)navigator->subcount && dirnumber > -1) {
                            navigator = navigator->parent;
                            position[dirnumber] = 0;
                            dirnumber--;
                        }
                        if (dirnumber < 0) {
                            dirnumber = 0;
                            break;
                        }
                    }
                }

                free(src->tab);
                src->tab = printJSON(compass);
                freeOBJ(compass);

            }
            checkpoint = strchr(checkpoint, '\n');
        }
        navigator = target;
        cursor = override;
    }
}

void createZip(FOLDER* folder) {
    char* path, location;
    path = calloc(PATH_MAX, sizeof(char));
    report = (char*)calloc(report_size, sizeof(char));

    if (path == NULL) {
        logger("Error allocating memory for start path, %s\n", strerror(errno));
        return;
    }
    if ((getcwd(path, PATH_MAX)) == NULL) {
        logger("getwcd() error, %s\n", strerror(errno));
        return;
    }
    if (path != NULL) {
        logger("Location Path <%s>\n", path);
    } else {
        logger("Invalid Path <%s>, %s\n", path, strerror(errno));
        return;
    }

    returnString(&path, "path");
    returnString(&path, folder->name);
    zipFile pack = zipOpen(folder->name, 0);

    if (pack == NULL) {
        logger("Failed to open a zip file for %s! %d", folder->name, ZIP_ERRNO);
        return;
    }

    //Loop through every content and print it
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
    lang = scanFolder(path, -1);
    returnString(&path, "path");
    returnString(&path, "resourcepacks");

    FOLDER* targets = createFolder(NULL, "targets");
    targets->subdir = (FOLDER*)calloc(2, sizeof(FOLDER));

    //Getting lang file
    translated = getLang(lang, 0);
        
    //Starting the menu;
    initscr();
    setlocale(LC_ALL, "");
    int input = 0, cursor[3], optLenght, actionLenght[2], type = 0, relay_message = -1, diretrix[2];
    cursor[0] = cursor[1] = cursor[2] = diretrix[0] = diretrix[1] = 0;
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
    optLenght = mvwprintLines(NULL, translated[0], 0, 1, 0, -1);

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
                        mvwprintLines(miniwin, translated[1], 1, 1, 1, 1);
                    }
                    while (entry != NULL && n_entries < 8) {
                        enQueue(entries, entry->d_name);
                        n_entries++;
                        entry = readdir(scan);
                    }
                }

                if (entries->end > 0) {
                    for (int x = 0; x < entries->end; x++) {
                        mvwprintw(miniwin, 1+x, 1, "%s [%c]  ", entries->item[x], (entries->value[x] == 1) ? 'X' : (entries->value[x] == 2) ? '#' : ' ');
                    }
                    for (int x = 0; x < query->end; x ++) {
                        mvwprintw(miniwin, 1 + query->value[x], strlen(entries->item[query->value[x]]) + 6, "%d", x + 1);
                    }
                } else {
                    mvwprintLines(miniwin, translated[1], 1, 1, 1, 1);
                }
                n_entries = entries->end;

                break;
            case 1:
                if (targets->subcount < 1) {
                    mvwprintLines(miniwin, translated[1], 1, 1, 3, 3);
                    n_entries = 0;
                } else {
                    for (int x = 0; x < 2; x++) {
                        optLenght = mvwprintLines(miniwin, translated[1], (1 + x), 1, (11 + x), (11 + x));
                        mvwprintw(miniwin, (x + 1), (optLenght + 2), "[%c]", (diretrix[x] == 1) ? 'X' : ' ');
                    }
                    n_entries = 2;
                }
                break;
            case 2:
                for (int x = 0; x < (int)lang->count; x++) {
                    mvwprintw(miniwin, x + 1, 1, lang->content[x].name);
                }

                n_entries = lang->count;
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
                optLenght = mvwprintLines(NULL, translated[1], (1 + cursor[0]), 1, (11 + cursor[0]), (11 + cursor[0]));
                mvwchgat(miniwin, cursor[0]+1, 1, optLenght + 4, A_STANDOUT, 0, NULL);
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

        wrefresh(action);
        wrefresh(miniwin);
        wrefresh(sidebar);
        
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
                mvwchgat(miniwin, cursor[0]+1, 1, optLenght + 4, A_NORMAL, 0, NULL);
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
            } else if (cursor[2] == 1 && cursor[0] < n_entries - 1) {
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
                    if (query->end > 0) {
                        type = 0;
                        relay_message = 5;
                        wclear(action);
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        cursor[0] = 0;
                        cursor[2] = 2;
                    }
                    break;
                case 1:
                    if (diretrix[0] == 1 || diretrix[1] == 1) {
                        type = 0;
                        relay_message = 10;
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        cursor[0] = 0;
                        cursor[2] = 2;
                    } else {
                        type = 1;
                        relay_message = 14;
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        cursor[0] = 0;
                        cursor[2] = 2;
                    }
                    break;
                case 3:
                    quit = true;
                    break;
                }
            } else if (cursor[2] == 1) {
                switch (cursor[1])
                {
                case 0:
                    if (entries->value[cursor[0]] == 2) {
                        break;
                    } else if (entries->value[cursor[0]] == 1) {
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
                        mvwprintw(miniwin, 1+x, 1, "%s [%c]  ", entries->item[x], (entries->value[x] == 1) ? 'X' : (entries->value[x] == 2) ? '#' : ' ');
                    }
                    for (int x = 0; x < query->end; x++) {
                        mvwprintw(miniwin, 1 + query->value[x], strlen(entries->item[query->value[x]]) + 6, "%d", x + 1);
                    }
                        
                    break;
                case 1:
                        switch (cursor[0])
                    {
                    case 0:
                        if (query->value[0] != 2) {
                            diretrix[0] = !diretrix[0];
                            optLenght = mvwprintLines(miniwin, translated[1], 1, 1, 11, 11);
                            mvwprintw(miniwin, 1, (optLenght + 2), "[%c]", (diretrix[0] == 1) ? 'X' : (diretrix[0] == 2) ? '#' : ' ');
                        }
                        break;
                    case 1:
                        if (query->value[1] != 2) {
                            diretrix[1] = !diretrix[1];
                            optLenght = mvwprintLines(miniwin, translated[1], 2, 1, 12, 12);
                            mvwprintw(miniwin, 2, (optLenght + 2), "[%c]", (diretrix[1] == 1) ? 'X' : (diretrix[1] == 2) ? '#' : ' ');
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case 2:
                    for (int x = 0; x < 3; x++) {
                        free(translated[x]);
                    }
                    free(translated);
                    translated = getLang(lang, cursor[0]);
                    updateWindows();
                    optLenght = mvwprintLines(NULL, translated[0], 0, 1, 0, -1);

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
                    if (cursor[0] == 0 && query->end > 0) {
                        type = 1;
                        FOLDER* temp;

                        for (int x = 0; x < query->end; x++) {
                            temp = scanFolder(path, query->value[x]);
                            addFolder(&targets, temp);
                            entries->value[query->value[x]] = 2;
                            temp = NULL;
                        }
                        
                        relay_message = 6;

                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                        endQueue(query);
                        query = initQueue(8);
                    } else {
                        cursor[2] = 0;
                        update = true;
                    }
                    break;
                case 1:
                    if (relay_message == 13 || relay_message == 14) {
                        wclear(action);
                        cursor[2] = 0;
                        update = true;
                    } else if (cursor[0] == 0) {
                        //Merge and Override
                        if (diretrix[0] == 1) {
                            for (int x = 0; x < (int)targets->subcount - 1; x++) {
                                overrideFiles(&targets->subdir[0], &targets->subdir[x + 1]);
                            }

                            wclear(action);
                            type = 1;
                            relay_message = 12;
                            confirmationDialog(translated[1], relay_message, actionLenght, 1);
                        }
                        //Diretrix execution
                        if (diretrix[1] == 1) {
                            for (int x = 0; x < (int)targets->subcount - 1; x++) {
                                executeCommand(&targets->subdir[0], &targets->subdir[x + 1]);
                            }

                            wclear(action);
                            type = 1;
                            relay_message = 12;
                            confirmationDialog(translated[1], relay_message, actionLenght, 1);
                        }

                        wclear(action);
                        type = 1;
                        relay_message = 13;
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
                    } else if (cursor[1] == 1) {
                        wclear(action);
                        cursor[2] = 0;
                        update = true;
                    } else {
                        wclear(action);
                        type = 1;
                        relay_message = 14;
                        confirmationDialog(translated[1], relay_message, actionLenght, type);
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
                    if (entries->end > 0) {
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