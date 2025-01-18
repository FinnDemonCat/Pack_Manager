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
#include <windows.h>
#include <math.h>
#include <time.h>
#include <locale.h>
#include <png.h>

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
    bool indent;
} OBJECT;

typedef struct TEXTURE {
	char *data;
    size_t size;
    size_t index;  
} TEXTURE;

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
    path[0] = '\0';

    while (strcmp(folder->name, "assets") != 0 && folder->parent != NULL) {
        enQueue(list, folder->name);
        folder = folder->parent;
    }

    for (size_t x = list->end; x > 0; x--) {
        if (length + strlen(list->item[x - 1]) >= (capacity - 1)) {
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

        sprintf(path + strlen(path), "%s/", list->item[x - 1]);
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

void pngErrLogger(png_structp png_ptr, png_const_charp message) {
	(void)png_ptr;
	logger("PNG > Error! %s\n", message);
}

void pngWarningLogger(png_structp png_ptr, png_const_charp message) {
	(void)png_ptr;
    logger("PNG > Warning! %s\n", message);
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
    file->value = NULL;
    file->indent = false;

    return file;
}

void addOBJ (OBJECT** file, OBJECT* value) {
    if (file[0]->count == 0) {
        file[0]->value = (OBJECT*)calloc(file[0]->capacity, sizeof(OBJECT));
        if (file[0] == NULL) {
            logger("Error allocating memory for new obj, %s\n", strerror(errno));
        }
    }
    if (file[0]->count == file[0]->capacity) {
        file[0]->capacity *= 2;

        OBJECT* temp = (OBJECT*)realloc(file[0]->value, file[0]->capacity * sizeof(OBJECT));
        if (temp == NULL) {
            logger("Error reallocating memory for %s\n", file[0]->declaration);
            return;
        } else {
            file[0]->value = temp;
        }
    }

    value->parent = file[0];
    file[0]->value[file[0]->count] = *value;
    file[0]->count++;
}

void delOBJ (OBJECT** file, size_t key) {
    while (file[0]->value[key].count > 0) {
		OBJECT* temp = &file[0]->value[key];
        delOBJ(&temp, 0);
    }

    free(file[0]->value[key].declaration);
    
    file[0]->count--;
    
    if (file[0]->value[key].value != NULL) {
        if (file[0]->value[key].count == 0) {
            free(file[0]->value[key].value); // It may not be working
        } else if (file[0]->value[key].count < file[0]->value[key].capacity / 2 && file[0]->value[key].capacity > 1) {
            file[0]->value[key].capacity /= 2;
            OBJECT* temp = realloc(file[0]->value[key].value, file[0]->value[key].capacity * sizeof(OBJECT));
            if (temp != NULL) {
                file[0]->value[key].value = temp;
            } else {
                logger("Failed to shrink memory when deleting an obj! %s\n", strerror(errno));
            }
        }
    }
    
    for (int x = key, y = file[0]->count; x < y; x++) {
        file[0]->value[x] = file[0]->value[x + 1];
    }
}

OBJECT* dupOBJ (OBJECT* target) {
    OBJECT* mirror = createOBJ(target->declaration);
	mirror->indent = target->indent;
    OBJECT* temp;
    for (int x = 0; x < (int) target->count; x++) {
        temp = dupOBJ(&target->value[x]);
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

	free(file);
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
        for (int x = 0; x < count; x++) {
            if (*str == chars[x]) {
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

void getNextStr (char** str, char* dest) {
	char *pointer = *str, *checkpoint;
	checkpoint = strchrs(pointer, 4, ' ', '\n', '\t', ';');

	if (checkpoint != NULL) {
		snprintf(dest, (size_t)(checkpoint - pointer) + 1, "%s", pointer);
		checkpoint = strnotchr(checkpoint, 4, ' ', '\n', '\t', ';');
		*str = checkpoint;
	} else {
		sscanf(pointer, "%s", dest);
		*str += strlen(pointer);
		return;
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
        case '\t':
            compass->indent = true;
            break;
        case ':':
            compass = &compass->value[compass->count - 1];
            x += 2;
            array = false;

            while (true) {
                pointer = strnotchr((obj + x), 3, ' ', ',', '\n');

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
                    for (int alpha = 1, beta = 1; alpha < (int)strlen(obj); alpha++) {
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
                    compass = compass->parent;
                    /* fallthrough */
                case '}':
                    x = (pointer - obj);
                    array = false;

                    break;
                case '\t':
                    compass->indent = true;
                    pointer = strnotchr((obj + x), 4, ',', '\t', ' ', '\n');
                    x = (pointer - obj);
                    break;
                default:
                    x = (pointer - obj);

                    if (*pointer == '\"') {
                        pointer = strchr(obj + x + 1, '\"');
                        pointer++;
                    } else {
                        pointer = strchrs(pointer, 6, ' ', ',', '\n', '\t', ']', '}');
                    }

                    length = (pointer - (obj + x));
                    sprintf(buffer, "%.*s", (int)length, obj + x);

                    entry = createOBJ(buffer);
                    addOBJ(&compass, entry);
                    entry = NULL;

                    x += length;
                    break;
                }
                
                if (!array) {
                    compass = compass->parent;
                    break;
                }
            }
            break;
		default:
		break;
        }
    }
    
    return compass;
}

void indentJSON (char** file) {
    char *pointer = *file, *temp;
    size_t count = 0;

    while ((pointer = strchr(pointer, '\n')) != NULL) {
        count++;
        pointer = strnotchr(pointer, 1, '\n');
    }

    if (count == 0) {
        return;
    }

    temp = (char*)realloc(*file, (strlen(*file) + count)* sizeof(char));
    if (temp != NULL) {
        *file = temp;
        temp = NULL;
    } else {
        logger("Error while reallocating memory when indenting file: %s\n", strerror(errno));
    }

    for (pointer = strchr(*file, '\n'); pointer != NULL && count > 1; pointer = strnotchr(pointer + 1, 1, '\t'), pointer = strchr(pointer, '\n'), count--) {
        memmove(pointer + 1, pointer, strlen(pointer) + 1);
        pointer[1] = '\t';
    }
}

char* printJSON (OBJECT* json) {
    char *pointer, *checkpoint, *buffer, *placeholder, *temp = NULL;
    size_t length = 0, size = 1024;
    OBJECT* navigator;
    temp = (char*)calloc(size, sizeof(char));
    *temp = '\0';
    
    //Printing current obj in placeholder
    navigator = json;
	if (strcmp(navigator->declaration, "obj") == 0 || strcmp(navigator->declaration, "array") == 0) {
        length = snprintf (NULL, 0,
            "%c%s%s%s%c",
            (strcmp(navigator->declaration, "obj") == 0) ? '{' : '[',
            (navigator->indent == true) ? "\n" : "", 
            "%s",
            (navigator->indent == true) ? "\n" : "",
            (strcmp(navigator->declaration, "obj") == 0) ? '}' : ']'
        );

        placeholder = (char*)calloc(length + 1, sizeof(char));
        
        sprintf (
            placeholder, 
            "%c%s%s%s%c",
            (strcmp(navigator->declaration, "obj") == 0) ? '{' : '[',
            (navigator->indent == true) ? "\n" : "", 
            "%s",
            (navigator->indent == true) ? "\n" : "",
            (strcmp(navigator->declaration, "obj") == 0) ? '}' : ']'
        );
    } else {
        if (navigator->count == 1) {
            length = snprintf(NULL, 0, "%s: %s", navigator->declaration, "%s");
            placeholder = (char*)calloc(length + 1, sizeof(char));
            sprintf(placeholder, "%s: %s", navigator->declaration, "%s");
        } else {
            placeholder = strdup(navigator->declaration);
        }
    }

    //Printing current obj values into temp
    for (int x = 0; x < (int)json->count; x++) {
        navigator = &json->value[x];
        pointer = printJSON(navigator);

        if (navigator->indent == true) {
            indentJSON(&pointer);
        }

        length = snprintf (
            NULL, 0,
            "%s%s",
            pointer,
            ((size_t)x == json->count - 1) ? "" : (json->indent == true) ? ",\n" : ", "
        );

        if (strlen(temp) + length >= size - 1) {
            while (strlen(temp) + length >= size - 1) {
                size *= 2;
            }
            checkpoint = realloc(temp, (size + 1) * sizeof(char));

            if (checkpoint == NULL) {
                logger("Error reallocating memory while printing the json file: %s\n", strerror(errno));
                free(placeholder);
                return NULL;
            } else {
                temp = checkpoint;
                checkpoint = NULL;
            }
        }

        sprintf (
            temp + strlen(temp),
            "%s%s",
            pointer,
            ((size_t)x == json->count - 1) ? "" : (json->indent == true) ? ",\n" : ", "
        );

        free(pointer);
    }
    
    length = snprintf(NULL, 0, placeholder, temp);
    buffer = (char*)calloc(length + 1, sizeof(char));
    sprintf(buffer, placeholder, temp);
    free(temp);
    free(placeholder);

    return buffer;
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

// Pass the folder with the target and it's index to free and move the array foward
void delFolder (FOLDER** folder, int index) {
	freeFolder(&folder[0]->subdir[index]);
	
	for (int x = index; x < (int)(folder[0]->subcount - 1); x++) {
		folder[0]->subdir[x] = folder[0]->subdir[x + 1];
	}
	folder[0]->subcount--;

	if (folder[0]->subcount == 0) {
		free(folder[0]->subdir);
	}
	if (folder[0]->subcount == folder[0]->capacity / 2 && folder[0]->capacity > 0) {
		folder[0]->capacity /= 2;
		FOLDER* backup = realloc(folder[0]->subdir, folder[0]->capacity * sizeof(FOLDER));

		if (backup == NULL) {
			logger("Couldn't trim %s folder capacity! %s\n", folder[0]->name, strerror(errno));
		} else {
			folder[0]->subdir = backup;
		}
	}
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
    FILE *file;
    ARCHIVE* model = (ARCHIVE*)malloc(sizeof(ARCHIVE));
    buffer = (char*)calloc(1025, sizeof(char));

	if (strstr(path, ".png") != NULL) {
		file = fopen(path, "rb");
	} else {
		file = fopen(path, "r");
	}

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
	folder[0]->count--;

    
    if (folder[0]->count == folder[0]->file_capacity/2 && folder[0]->file_capacity/2 > 0) {
		folder[0]->file_capacity /= 2;
        ARCHIVE* backup = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity * sizeof(ARCHIVE));
        if (backup == NULL) {
            logger("Couldn't trim %s content size\n", folder[0]->name);
        } else {
            folder[0]->content = backup;
        }
    }
}

ARCHIVE* dupFile(ARCHIVE* file) {
    ARCHIVE* dup = (ARCHIVE*)malloc(sizeof(ARCHIVE));

    dup->name = strdup(file->name);
    dup->size = file->size;

	if (strstr(dup->name, ".png") == NULL) {
    	dup->tab = strdup(file->tab);
	} else {
		dup->tab = malloc(dup->size);
		memcpy(dup->tab, file->tab, file->size);
	}

    return dup;
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
FOLDER* getFolder(char* path, int position) {
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

void copyOverides (ARCHIVE* file, ARCHIVE* overrides) {
    OBJECT *base, *predicates, *pointer, *checkpoint;
    QUEUE *matches;

    pointer = checkpoint = NULL;

    if (strstr(overrides->name, ".json") != NULL || strstr(file->name, ".json") != NULL) {
        base = processOBJ(file->tab);
        predicates = processOBJ(overrides->tab);

        for (size_t x = 0; x < base->count; x++) {
            if (strcmp(base->value[x].declaration, "\"overrides\"") == 0) {
                pointer = base->value[x].value;
            }
        }
        
        for (size_t x = 0; x < predicates->count; x++) {
            if (strcmp(predicates->value[x].declaration, "\"overrides\"") == 0) {
                checkpoint = predicates->value[x].value;
            }
        }

        if (pointer == NULL || checkpoint == NULL) {
            return;
        }

        matches = initQueue(pointer->count);

        for (size_t x = 0; x < pointer->count; x++) {
            pointer = &pointer->value[x];

            for (size_t y = 0; y > pointer->count; y++) {
                if (strcmp(pointer->value[y].declaration, "\"model\"") == 0) {
                    enQueue(matches, pointer->value[y].value->declaration);
                    matches->value[x] = x;
                    break;
                }
            }
            pointer = pointer->parent;
        }

        for (size_t x = 0; x < checkpoint->count; x++) {
            checkpoint = &checkpoint->value[x];

            for (size_t y = 0; y > checkpoint->count; y++) {
                if (strcmp(pointer->value[y].declaration, "\"model\"") == 0 && strcmp(checkpoint->value[y].declaration, matches->item[x]) == 0) {
                    deQueue(matches, x);
                    logger("Found predicate model match \"%s\" at %s\n", matches->item[x], file->name);
                    break;
                }
            }
            checkpoint = checkpoint->parent;
        }

        for (int x = 0; x < matches->end; x++) {
            addOBJ(&pointer, &checkpoint->value[matches->value[x]]);
        }

        endQueue(matches);
        free(file->tab);
        file->tab = printJSON(base);
        file->size = strlen(file->tab);
        freeOBJ(base);
        freeOBJ(predicates);

    }
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
                        
                        copyOverides(&cursor->content[files->value[y]], &navigator->content[x]);

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

void readPNGFile (png_structp image, png_bytep data, png_size_t size) {
	TEXTURE* reader = (TEXTURE*)png_get_io_ptr(image);

	if (reader->index + size > reader->size) {
		pngErrLogger(image, "Reading beyond of the buffer!\n");
	}

	memcpy(data, reader->data + reader->index, size);
	reader->index += size;
}

void getPNGDimentions (char* image, size_t size, int *height, int *width) {
	TEXTURE reader = {
		image, size, 0
	};

	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);

	png_set_read_fn(png, &reader, readPNGFile);
	png_read_info(png, info);

	*height = png_get_image_height(png, info);
	*width = png_get_image_width(png, info);

	png_destroy_read_struct(&png, &info, NULL);
}

png_bytep* processPNG (char* image, size_t size) {
	size_t row_size, height;
	png_bytep *row_pointers;

	TEXTURE reader = {
		image, size, 0
	};

	// Creating png structs
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	png_infop info = png_create_info_struct(png);

	// Setting up custom reading
	png_set_read_fn(png, &reader, readPNGFile);
	png_read_info(png, info);

	// Getting dimentions and color information
    height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

	// Adjusting mode to RGB if necessary
	if (color_type == PNG_COLOR_TYPE_PALETTE) {
		png_set_palette_to_rgb(png);
	}
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
		png_set_expand_gray_1_2_4_to_8(png);
	}
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
		png_set_tRNS_to_alpha(png);
	}
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY) {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
	}

	// Updating png structs info
	png_read_update_info(png, info);
	row_size = png_get_rowbytes(png, info);

	row_pointers = (png_bytep *)malloc(height * sizeof(png_bytep));
    if (!row_pointers) {
        logger("Fail to allocate memory while reading the png: %s\n", strerror(errno));
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    for (int y = 0; y < (int)height; y++) {
        row_pointers[y] = (png_bytep)malloc(row_size);
        if (!row_pointers[y]) {
            logger("Fail to allocate memory for image line: %s\n", strerror(errno));

            for (int j = 0; j < y; j++) {
                free(row_pointers[j]);
            }
            free(row_pointers);
            png_destroy_read_struct(&png, &info, NULL);
            return NULL;
        }
    }
	
	//Linking pointer array to char matrix
    png_read_image(png, row_pointers);
	png_destroy_read_struct(&png, &info, NULL);
    return row_pointers;
}

void writePNGFile (png_structp png, png_bytep data, png_size_t size) {
	TEXTURE* writer = (TEXTURE*)png_get_io_ptr(png);

	if (writer->size + size > writer->index) {
        writer->index *= 2;
        writer->data = realloc(writer->data, writer->index);
    }

	memcpy(writer->data + writer->size, data, size);
    writer->size += size;
}

void printToPNGFile (ARCHIVE** image, png_bytep *pixels) {
	size_t input_size = image[0]->size;
	char* file = image[0]->tab;

	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) return;

    png_infop data = png_create_info_struct(png);
    if (!data) {
        png_destroy_write_struct(&png, NULL);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &data);
        return;
    }

	TEXTURE reader = {
		file, input_size, 0
	};

	png_set_read_fn(png, &reader, readPNGFile);
	png_read_info(png, data);

	size_t width = png_get_image_width(png, data);
    size_t height = png_get_image_height(png, data);
    int color_type = png_get_color_type(png, data);
    int bit_depth = png_get_bit_depth(png, data);

	png_structp write_png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!write_png) {
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	png_infop write_info = png_create_info_struct(write_png);
    if (!write_info) {
        png_destroy_write_struct(&write_png, NULL);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	if (setjmp(png_jmpbuf(write_png))) {
        png_destroy_write_struct(&write_png, &write_info);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	TEXTURE buffer = { malloc(1024), 0, 1024 };
    if (!buffer.data) {
        png_destroy_write_struct(&write_png, &write_info);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	png_set_write_fn(write_png, &buffer, writePNGFile, NULL);

	png_set_IHDR(
        write_png, write_info,
        width, height,
        bit_depth,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

	png_write_info(write_png, write_info);

	png_write_image(write_png, pixels);
    png_write_end(write_png, NULL);

	png_destroy_write_struct(&write_png, &write_info);
    png_destroy_read_struct(&png, &data, NULL);

	free(image[0]->tab);
	image[0]->tab = buffer.data;
}

void resizePNGFile(ARCHIVE** image, size_t height, size_t width) {
	png_bytep* file = processPNG(image[0]->tab, image[0]->size), *resize;
	int lines, collums;
	size_t row_size;
	
	//Setting up the reader
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) return;

    png_infop data = png_create_info_struct(png);
    if (!data) {
        png_destroy_write_struct(&png, NULL);
        return;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &data);
        return;
    }

	TEXTURE reader = {
		image[0]->tab, image[0]->size, 0
	};

	png_set_read_fn(png, &reader, readPNGFile);
	png_read_info(png, data);

	collums = png_get_image_width(png, data);
    lines = png_get_image_height(png, data);
    int color_type = png_get_color_type(png, data);
    int bit_depth = png_get_bit_depth(png, data);

	png_read_update_info(png, data);
	row_size = png_get_rowbytes(png, data);

	png_structp write_png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!write_png) {
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	png_infop write_info = png_create_info_struct(write_png);
    if (!write_info) {
        png_destroy_write_struct(&write_png, NULL);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	if (setjmp(png_jmpbuf(write_png))) {
        png_destroy_write_struct(&write_png, &write_info);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	TEXTURE buffer = { malloc(1024), 0, 1024 };
    if (!buffer.data) {
        png_destroy_write_struct(&write_png, &write_info);
        png_destroy_read_struct(&png, &data, NULL);
        return;
    }

	png_set_write_fn(write_png, &buffer, writePNGFile, NULL);

	png_set_IHDR(
        write_png, write_info,
        width, height,
        bit_depth,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

	//Resizing 
	resize = (png_bytep*)malloc(height * sizeof(png_bytep));
	for (size_t y = 0; y < height; y++) {
		resize[y] = (png_bytep)malloc(row_size);

		if (resize[y] == NULL) {
			logger("Fail to allocate memory for image line: %s\n", strerror(errno));

			for (size_t x = 0; x < y; x++) {
				free(resize[x]);
			}

			free(resize);
			return;
		}
	}

	for (size_t x = 0; x < height; x++) {

		for (size_t y = 0; y < width; y++) {
			png_bytep pixel, texture;
			pixel = &resize[x][(y * 4)];

			if (x < (size_t)lines && y < (size_t)collums) {
				texture = &file[x][(y * 4)];

				pixel[0] = texture[0];
				pixel[1] = texture[1];
				pixel[2] = texture[2];
				pixel[3] = texture[3];
			} else {

				pixel[3] = 0;
			}
		}
	}

	//Writing
	png_write_info(write_png, write_info);
	png_write_image(write_png, resize);
    png_write_end(write_png, NULL);

	png_destroy_write_struct(&write_png, &write_info);
	png_destroy_read_struct(&png, &data, NULL);

	free(image[0]->tab);
	image[0]->tab = buffer.data;
}

void overridesFormatConvert(FOLDER* folder, ARCHIVE** file) {
	OBJECT *placeholder, *overrides, *query, *value, *model, *temp;
	QUEUE *types, *list;
	FOLDER* location;
	char buffer[1024], *pointer, name[256];
	int type = 0;
	bool custom_model_data, crossbow, pulling;
	custom_model_data = crossbow = false;

	// 1 - Queue all the predicates type
	// 2 - Create the components file to acomodade them
	// 3 - Sort the entries

	location = localizeFolder(folder, "minecraft:items/", true);

	pointer = strchr(file[0]->name, '.');
	snprintf(name, (pointer - file[0]->name) + 1, "%s", file[0]->name);

	overrides = processOBJ(file[0]->tab);
	placeholder = processOBJ("{\n\t\"model\": {}\n}");
	placeholder = placeholder->value;
	freeOBJ(placeholder->value);
	placeholder->count--;
	placeholder->value = NULL;

	// Locating the overrides
	for (size_t x = 0; x < overrides->count; x++) {
		if (strcmp(overrides->value[x].declaration, "\"overrides\"") == 0) {
			overrides = &overrides->value[x];
			break;
		}
	}

	if (strcmp(overrides->declaration, "\"overrides\"") != 0) {
		logger("Failed to locate \"overrides\" at %s", file[0]->name);
		return;
	}
	overrides = overrides->value;
	types = initQueue(overrides->count);

	logger("Starting components translation for %s\n", file[0]->name);

	for (size_t x = 0; x < overrides->count; x++) {
		for (size_t y = 0; y < overrides->value[x].count; y++) {
			// Pointing to the predicates
			if (strcmp(overrides->value[x].value[y].declaration, "\"predicate\"") == 0) {
				query = overrides->value[x].value[y].value;
				break;
			}
		}

		// Queueing the predictes
		list = initQueue(query->count);
		for (size_t y = 0; y < query->count; y++) {
			list->value[list->end] = y;
			enQueue(list, query->value[y].declaration);
		}

		// Dequeuing repeats
		for (size_t y = 0; y < (size_t)types->end; y++) {
			for (size_t z = 0; z < (size_t)list->end;) {
				if (strcmp(types->item[y], list->item[z]) == 0) {
					deQueue(list, z);
					break;
				} else {
					z++;
				}
			}
		}

		// Adding the uniques
		for (size_t y = 0; y < (size_t)list->end; y++) {
			enQueue(types, list->item[y]);
			logger("Found \"%s\" predicate\n", list->item[y]);
		}

		endQueue(list);
	}

	if (strstr(file[0]->name, "bow") != NULL) {
		type = 1;
		if (strstr(file[0]->name, "crossbow") != NULL) {
			crossbow = true;
		} 
	} else {
		type = 2;
	}

	switch (type)
	{
	case 1:
		// It's a bow / crossbow
		temp = processOBJ("{\n\t\"type\": \"minecraft:condition\",\n\t\"property\": \"minecraft:using_item\",\n\t\"on_true\": {},\n\t\"on_false\": {}\n}");
		addOBJ(&placeholder, temp);
		temp = NULL;

		freeOBJ(placeholder->value->value[2].value); // "on_true"
		freeOBJ(placeholder->value->value[3].value); // "on_false"
		placeholder->value->value[2].count--;
		placeholder->value->value[3].count--;
		placeholder->value->value[2].value = NULL;
		placeholder->value->value[3].value = NULL;
		
		sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:%s\",\n\t\"entries\": [\n\t\t\n\t],\n\t\"fallback\": {\n\t\t\"type\": \"minecraft:model\",\n\t\t\"model\": \"minecraft:item/%s\"\n\t}\n}", crossbow == true ? "crossbow/pull" : "use_duration", file[0]->name);
		temp = processOBJ(buffer);
		value = &placeholder->value->value[2];
		addOBJ(&value, temp);
		temp = NULL;

		for (size_t x = 0; x < (size_t)types->end; x++) {
			if (strcmp(types->item[x], "\"custom_model_data\"") == 0) {
				OBJECT* mirror;
				custom_model_data = true;
				// "on_true" -> "entries"
				sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:custom_model_data\",\n\t\"entries\": [\n\t\n],\n\t\"fallback\": {}");
				temp = processOBJ(buffer);

				freeOBJ(temp->value[3].value);
				temp->value[3].value = NULL;
				temp->value[3].count--;

				value = &placeholder->value->value[2].value->value[3]; // fallback
				mirror = dupOBJ(temp);
				addOBJ(&value, mirror);
				mirror = NULL;

				value = &placeholder->value->value[3]; // on_false
				sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:custom_model_data\",\n\t\"entries\": [\n\t],\n\t\"fallback\": {\n\t\t\"type\": \"minecraft:model\",\n\t\t\"model\": \"minecraft:item/%s\"\n\t}\n}", name);
				temp = processOBJ(buffer);
				addOBJ(&value, temp);
				temp = NULL;
				
				break;
			}
		}

		// Sorting models
		for (size_t x = 0; x < overrides->count; x++) {
			float index = -1;
			pulling = false;

			for (size_t y = 0; y < overrides->value[x].count; y++) {
				if (strcmp(overrides->value[x].value[y].declaration, "\"predicate\"") == 0) {
					query = overrides->value[x].value[y].value;
				} else if (strcmp(overrides->value[x].value[y].declaration, "\"model\"") == 0) {
					model = overrides->value[x].value[y].value;
				}
			}

			for (size_t y = 0; y < query->count; y++) {
				if (strcmp(query->value[y].declaration, "\"pulling\"") == 0) {
					pulling = true;
					index = strtof(query->value[y].value->declaration, NULL);
					break;
				}
			}

			for (size_t y = 0; y < query->count; y++) {
				if (strcmp(query->value[y].declaration, "\"pull\"") == 0) {
					index = strtof(query->value[y].value->declaration, NULL);
					pulling = true;

					if (index == 0) {
						type = 1;
						value = &placeholder->value->value[2].value->value[3]; // using true -> range dispatch -> fallback
					} else {
						type = 2;
						value = &placeholder->value->value[2].value->value[2]; // using true -> range dispatch -> entries

						for (size_t z = 0; z < (value->value->count + 1); z++) {
							if (z < value->value->count && index == strtof(value->value->value[z].value[1].value->declaration, NULL)) {
								value = &value->value->value[z].value[0]; // Pointing to the respective pull value's model if already set
								break;
							} else if (z >= value->value->count) {
								sprintf(buffer, "{\n\t\"model\": {},\n\t\"threshold\": %.2f\n}", index);
								temp = processOBJ(buffer);
								freeOBJ(temp->value[0].value);
								temp->value[0].value = NULL;
								temp->value[0].count--;

								addOBJ(&value->value, temp);
								value = &temp->value[0];
								temp = NULL;

								break;
							}
						}
					}
					break;
				} else if (pulling == false) {
					value = &placeholder->value->value[3];
					break;
				}
			}

			index = -1;
			
			// Search for the respective cmd index
			for (size_t y = 0; custom_model_data && y < (query->count + 1); y++) {
				// handling adding range_dispatch when the current entry doesn't have
				if (value->value == NULL) {
					sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:custom_model_data\",\n\t\"entries\": [\n\t],\n\t\"fallback\": {}");
					temp = processOBJ(buffer);

					freeOBJ(temp->value[3].value);
					temp->value[3].value = NULL;
					temp->value[3].count--;

					addOBJ(&value, temp);
					temp = NULL;
				}
				
				if (y < query->count && strcmp(query->value[y].declaration, "\"custom_model_data\"") == 0) {
					index = strtof(query->value[y].value->declaration, NULL);
					value = &value->value->value[2]; //Pointing to entries

					for (size_t z = 0; z < (value->value->count + 1); z++) {
						if (z < value->value->count && index == strtof(value->value->value[z].value[1].value->declaration, NULL)) {
							value = &value->value->value[z].value[0];
							break;
						} else if (z >= value->value->count) {
							sprintf(buffer, "{\n\t\"model\": {},\n\t\"threshold\": %d\n}", (int)index);
							temp = processOBJ(buffer);
							freeOBJ(temp->value[0].value);
							temp->value[0].count--;

							addOBJ(&value->value, temp);
							value = &temp->value[0];
							temp = NULL;

							break;
						}
					}

					break;
				} else if (y >= query->count) {
					value = &value->value->value[3]; //Pointing to fallback when the predicate doesn't have cmd predicate
					break;
				}
			}

			// Search for the respective charge type when "being used" is false
			for (size_t y = 0; pulling == false && crossbow == true && y < (query->count + 1); y++) {
				index = 0;
				
				if (value->value == NULL) {
					sprintf(buffer, "{\n\t\"type\": \"minecraft:select\",\n\t\"property\": \"minecraft:charge_type\",\n\t\"cases\": [\n\n\t],\n\t\"fallback\": {\n\t\t\"type\": \"minecraft:model\",\n\t\t\"model\": \"minecraft:item/%s\"\n\t}\n}", file[0]->name);
					temp = processOBJ(buffer);

					addOBJ(&value, temp);
					temp = NULL;
				}
				
				if (y < query->count && strcmp(query->value[y].declaration, "\"firework\"") == 0) {
					value = &value->value->value[2]; //Pointing to entries
					index = strtof(query->value[y].value->declaration, NULL);

					for (size_t z = 0; z < (value->value->count + 1); z++) {
						
						if (z < value->value->count && (index == 1 && strcmp(value->value->value[z].value[1].value->declaration, "\"rocket\"") == 0)) {
							value = &value->value->value[z].value[0];

							break;
						} else if (z >= value->value->count) {
							sprintf(buffer, "{\n\t\"model\": {},\n\t\"when\": \"rocket\"\n}");
							temp = processOBJ(buffer);
							freeOBJ(temp->value[0].value);
							temp->value[0].count--;

							addOBJ(&value->value, temp);
							value = &temp->value[0];
							temp = NULL;

							break;
						}
					}

					break;
				} else if (y >= query->count) {
					value = &value->value->value[2]; //Pointing to entries

					sprintf(buffer, "{\n\t\"model\": {},\n\t\"when\": \"arrow\"\n}");
					temp = processOBJ(buffer);
					freeOBJ(temp->value[0].value);
					temp->value[0].count--;

					addOBJ(&value->value, temp);
					value = &temp->value[0];
					temp = NULL;

					break;
				}
			}

			pointer = strrchr(model->declaration, '/');
			sscanf(pointer + 1, "%255[^\"]", name);
			sprintf(buffer, "{\n\t\"type\": \"minecraft:model\",\n\t\"model\": \"minecraft:item/%s\"\n}", name);

			temp = processOBJ(buffer);
			addOBJ(&value, temp);
			temp = NULL;
		}

		break;
	case 2:
		// It's a regular model
		break;
	
	default:
		break;
	}

	placeholder = placeholder->parent;
	ARCHIVE* item = (ARCHIVE*)malloc(sizeof(ARCHIVE));
	item->name = strdup(file[0]->name);
	item->tab = printJSON(placeholder);
	indentJSON(&item->tab);
	item->size = strlen(item->tab);

	addFile(&location, item);
	item = NULL;
}

void executeInstruct(FOLDER* target, FOLDER* assets, char* instruct) {
	FOLDER *navigator, *cursor, *templates;
	char *pointer, *checkpoint, *save, buffer[1024], namespace[512], command[256], name[256], *path = calloc(256, sizeof(char));
	ARCHIVE *file, *destination;
	OBJECT *placeholder, *value, *query;
    int line_number = 0, type;

	pointer = instruct;
	getcwd(path, 256);
	returnString(&path, "templates");

	//Temp
	for (size_t x = 0; x < assets->count; x++) {
		if (strstr(assets->content[x].name, ".instruct") != NULL) {
			instruct = assets->content[x].tab;
			break;
		}
	}

	templates = getFolder(path, -1);
	pointer = instruct;
	while ((pointer = strchrs(pointer, 2, '>', '<')) != NULL) {
		type = 0;
		file = destination = NULL;

		sscanf(pointer + 2, "%[^:]255", command);
		switch (pointer[0]) {
			case '>':
				navigator = localizeFolder(assets, command, true);
				break;
			case '<':
				navigator = localizeFolder(target, command, true);
				break;
		}

		save = strrchr(command, '/');
		if (save != NULL) {
			sscanf(save + 1, "%[^\"]255", name);
		} else {
			strcpy(name, command);
		}

		if (command[strlen(command) - 1] == '/' || command[strlen(command) - 1] == '\\') {
			type = 1;
			logger("Target is a folder\n");
			line_number++;
		}

		for (size_t x = 0; x < navigator->count && type == 0; x++) {
			if (strcmp(name, navigator->content[x].name) == 0) {
				file = &navigator->content[x];
				break;
			}
		}

		if (file == NULL && type == 0) {
			logger("File %s was not found! Skipping to next iteration\n", name);
			line_number++;
			continue;
		}

		if (strstr(file->name, ".json") != NULL) {
            placeholder = processOBJ(file->tab); //Check if it's a json file
        }

		pointer = strchr(pointer, ':');
		checkpoint = strchrs(pointer, 2, '>', '<');
		snprintf(buffer, (int)(checkpoint - pointer) - 1, "%s", pointer + 1);

		checkpoint = &buffer[0];

		while (checkpoint != NULL) {
			checkpoint = strnotchr(checkpoint, 4, ';', ' ', '\n', '\t');

			getNextStr(&checkpoint, command);

			if (strstr(command, "move") != NULL || strstr(command, "copy") != NULL) {
				getNextStr(&checkpoint, command);
				sscanf(command + 1, "%511[^\"]", namespace);

				cursor = localizeFolder(target, namespace, true);
				if (type == 0) {
					ARCHIVE* mirror = NULL;
					save = strrchr(namespace, '/');

					if (strlen(save) > 1) {
						sscanf(save + 1, "%[^\"]255", name);

						for (size_t x = 0; x < cursor->count; x++) {
							if (strcmp(name, cursor->content[x].name) == 0) {
								logger("A matching file to %s was found, copying the override contents\n", name);
								line_number++;
								copyOverides(file, &cursor->content[x]);

								delFile(&cursor, x);
								break;
							}
						}
					}

					if (strcmp(command, "copy") == 0) {
						mirror = dupFile(file);
						addFile(&cursor, mirror);
					} else {
						mirror = dupFile(file);
						addFile(&cursor, mirror);

						for (size_t x = 0; x < navigator->count; x++) {
							if (strcmp(navigator->content[x].name, name) == 0) {
								delFile(&navigator, x);
								break;
							}
						}
					}
				}  else {
					FOLDER *mirror = NULL;
					if (strcmp(navigator->name, cursor->name) == 0) {
						logger("Folder destination has the same name, merging the contents\n");
						line_number++;

						overrideFiles(cursor, navigator);

						cursor = navigator;
						navigator = navigator->parent;

						for (size_t x = 0; x < navigator->subcount; x++) {
							if (strcmp(cursor->name, navigator->subdir[x].name) == 0) {
								delFolder(&navigator, x);
								break;
							}
						}
					} else {
						mirror = dupFolder(navigator);

						addFolder(&cursor, mirror);
						mirror = NULL;
					}
				}
			} else if (strcmp(command, "edit") == 0) {
				getNextStr(&checkpoint, command);

				if (strcmp(command, "name") == 0) {
					getNextStr(&checkpoint, command);
					sscanf(command, "\"%255[^\"]\"", name);

					if (type == 1) {
						free(navigator->name);
						navigator->name = strdup(name);
					} else {
						free(file->name);
						file->name = strdup(name);
					}
				} else if (strcmp(command, "display") == 0) {
					char par[1024];

					checkpoint = strchr(checkpoint, '{');
					for (int x = 1, y = 1; checkpoint[x] != '\0'; x++) {
						switch (checkpoint[x]) {
							case '{':
								y++;
								break;
							case '}':
								y--;
								break;
						}

						if (y == 0) {
							snprintf(par, x, "%s", checkpoint);
							break;
						}
					}

					value = processOBJ(par);
					checkpoint += strlen(par);
					
					for (size_t x = 0; x < placeholder->count; x++) {
						if (strcmp(placeholder->value[x].declaration, "\"display\"") == 0) {
							query = placeholder->value[x].value;
							break;
						}
					}

					//Finding display type
					for (size_t x = 0; x < query->count; x++) {
						if (strcmp(query->value[x].declaration, value->value->declaration) == 0) {
							OBJECT* temp = &query->value[x];
							query->value[x] = *value->value;
							
							freeOBJ(temp);
							value->value = NULL;
							break;
						}
					}

					if (value->value != NULL) {
						addOBJ(&query, value->value);
						value->value = NULL;
					}
					freeOBJ(value);
					
				} else if (strcmp(command, "texture_path") == 0) {
					if (checkpoint[0] != '{') {
						getNextStr(&checkpoint, command);
					} else {
						command[0] = '\0';
					}
					value = query = NULL;

					for (size_t x = 0; x < placeholder->count; x++) {
						if (strcmp(placeholder->value[x].declaration, "\"textures\"") == 0) {
							query = &placeholder->value[x];
							break;
						}
					}

					if (query == NULL) {
						logger("textures member wasn't found at %s, the program will add manually\n", file->name);
						line_number++;

						query = createOBJ("\"textures\"");
						value = createOBJ("obj");

						addOBJ(&query, value);
						value = NULL;
					}

					query = query->value;

					if (strcmp(command, "set") == 0) {
						while (query->count > 0)
						{
							delOBJ(&query, 0);
						}
					}

					sscanf(checkpoint, "%511[^;]", namespace);
					checkpoint += strlen(namespace);
					value = processOBJ(namespace);

					if (strcmp(value->value->value->declaration, "\"self\"") == 0) {
						logger("\"self\" was defined to %s", query->value[0].declaration); 
						free(value->value->value->declaration);
						value->value->value->declaration = strdup(query->value[0].value->declaration);
					}

					for (size_t x = 0; x < query->count; x++) {
						if (strcmp(query->value[x].declaration, value->value->declaration) == 0) {
							freeOBJ(&query->value[x]);
							query->value[x] = *value;

							value = NULL;
						}
					}

					if (value != NULL) {
						addOBJ(&query, value->value);
						value->value = NULL;
						freeOBJ(value);
					}
				} else if (strcmp(command, "dimentions") == 0) {
					getNextStr(&checkpoint, command);
					size_t width = 0, height = 0;
					sscanf(command, "%llux%llu", &width, &height);

					resizePNGFile(&file, height, width);

					FILE* test = fopen("c:\\Users\\Calie\\Downloads\\test.png", "wb+"); // This paint job isn't working for some reason
					fwrite(file->tab, 1, file->size, test);
					fclose(test);
				}

			} else if (strstr(command, "autofill") != NULL) {
				QUEUE* folders;
				int dirnumber = 0, position[16];
				char *temp;
                size_t length;

				for (int x = 0; x < 16; x++) {
                    position[x] = 0;
                }

				while(placeholder->value->value->count > 0) {
					delOBJ(&placeholder->value->value, 0);
				}
				
                navigator = localizeFolder(navigator, "minecraft:textures/", false);
                if (navigator == NULL) {
                    logger("Error! no textures folder were found in the minecraft folder!\n");
                    line_number++;
                    break;
                }

				navigator = navigator->parent->parent;
                folders = initQueue(navigator->subcount - 1);

                for (size_t x = 0; x < navigator->subcount; x++) {
                    if (strcmp(navigator->subdir[x].name, "minecraft") == 0) {
                        continue;
                    }

                    enQueue(folders, navigator->subdir[x].name);
                }

				if (folders->end == 0) {
                    logger("No other folders besides minecraft were found.\n");
                    break;
                }

				for (size_t x = 0; x < templates->count; x++) {
                    if (strcmp(templates->content[x].name, "atlases.txt") == 0) {
                        query = processOBJ(templates->content[x].tab);
                        query = query->value->value;
                        break;
                    }
                }

				if (query == NULL) {
                    logger("Error! atlas.json template is missing from program files!\n");
                    break;
                }

				for (int x = 0; x < folders->end; x++) {
                    char* stamp;
                    sprintf(namespace, "%s:textures/", folders->item[x]);

                    navigator = localizeFolder(target, namespace, false);

                    while (dirnumber >= 0) {
                        while (position[dirnumber] < (int)navigator->subcount) {
                            navigator = &navigator->subdir[position[dirnumber]];

                            position[dirnumber]++;
                            dirnumber++;
                        }

                        temp = returnPath(navigator);
						stamp = strchr(temp, '/');
						stamp[0] = ':';
                        temp[strlen(temp) - 1] = '\0';

                        if (navigator->count == 1) {
                            value = dupOBJ(&query->value[1]);

                            length = snprintf(NULL, 0, "\"%s/%s\"", temp, navigator->content->name);
                            stamp = (char*)calloc(length + 1, sizeof(char));
                            sprintf(stamp, "\"%s/%s\"", temp, navigator->content->name);
                            free(value->value[1].value->declaration);
                            value->value[1].value->declaration = stamp;
                            stamp = NULL;

                            addOBJ(&placeholder->value->value, value);
                            value = NULL;

                        } else if (navigator->count > 1) {
                            value = dupOBJ(&query->value[0]);

                            length = snprintf(NULL, 0, "\"%s\"", temp);
                            stamp = (char*)calloc(length + 1, sizeof(char));
                            sprintf(stamp, "\"%s\"", temp);
                            free(value->value[1].value->declaration);
                            value->value[1].value->declaration = stamp;
                            stamp = NULL;

                            length = snprintf(NULL, 0, "\"%s/\"", temp);
                            stamp = (char*)calloc(length + 1, sizeof(char));
                            sprintf(stamp, "\"%s/\"", temp);
                            free(value->value[2].value->declaration);
                            value->value[2].value->declaration = stamp;
                            stamp = NULL;

                            addOBJ(&placeholder->value->value, value);
                            value = NULL;
                        }

                        while (position[dirnumber] == (int)navigator->subcount && dirnumber > -1) {
                            navigator = navigator->parent;
                            position[dirnumber] = 0;
                            dirnumber--;
                        }
                        
                        free(temp);
                        
                        if (dirnumber < 0) {
                            dirnumber = 0;
                            break;
                        }
                    }
                }

                free(file->tab);
                file->tab = printJSON(placeholder);
				indentJSON(&file->tab);
                file->size = strlen(file->tab);
				
			} else if (strcmp(command, "remove") == 0) {
				if (type == 0) {
					for (size_t x = 0; x < navigator->count; x++) {
						if (strcmp(navigator->content[x].name, file->name) == 0) {
							delFile(&navigator, x);
							break;
						}
					}
				} else {
					cursor = navigator;
					navigator = navigator->parent;

					for (size_t x = 0; x < navigator->subcount; x++) {
						if (strcmp(navigator->subdir[x].name, cursor->name) == 0) {
							delFolder(&navigator, x);
							break;
						}
					}
				}

			} else if (strcmp(command, "disassemble") == 0) {
				OBJECT *mirror, *copy, *elements, *cube, *base = NULL, *children;
				QUEUE *groups;
				FOLDER* permutate_destination;
				bool trim = false;

				getNextStr(&checkpoint, command);

				if (strcmp(command, "trim") == 0) {
					trim = true;

					checkpoint = strchr(checkpoint + 1, '\"');
					sscanf(checkpoint + 1, "%255[^\"]", command);
				}

				//Cleaning the reference
				mirror = dupOBJ(placeholder);
				for (size_t x = 0; x < mirror->count; x++) {
					if (strcmp(mirror->value[x].declaration, "\"elements\"") == 0) {
						query = mirror->value[x].value;
						while (query->count > 0) {
							delOBJ(&query, 0);
						}

					} else if (strcmp(mirror->value[x].declaration, "\"groups\"") == 0) {
						query = &mirror->value[x];
						delOBJ(&mirror, x);

					}
				}

				//QUEUEing the groups names and position
				for (size_t x = 0; x < placeholder->count; x++) {
					if (strcmp(placeholder->value[x].declaration, "\"elements\"") == 0) {
						elements = placeholder->value[x].value;
					} else if (strcmp(placeholder->value[x].declaration, "\"groups\"") == 0) {
						query = placeholder->value[x].value;
						groups = initQueue(query->count);

						 for (size_t y = 0; y < query->count; y++) {

							for (size_t z = 0; z < query->value[y].count; z++) {

								if (strcmp(query->value[y].value[z].declaration, "\"name\"") == 0) {

									if (strcmp(query->value[y].value[z].value->declaration, "\"base\"") == 0) {
										base = &query->value[y].value[z];
									} else {
										enQueue(groups, query->value[y].value[z].value->declaration);
										groups->value[groups->end - 1] = y;
									}

									break;
								}
							}
						 }
					}
				}

				//OBJECT value, copy, cube
				//Template it's OBJECT mirror

				for (size_t x = 0; x < (size_t)groups->end; x++) {
					value = dupOBJ(mirror);
					int index;
					ARCHIVE* permutate;
					char file_name[256];

					for (size_t y = 0; y < value->count; y++) {

						if (strcmp(value->value[y].declaration, "\"elements\"") == 0) {
							copy = value->value[y].value;
							break;
						}
					}

					if (base != NULL) {
						for (size_t x = 0; x < base->count; x++) {

							if (strcmp(base->value[x].declaration, "\"children\"") == 0) {

								for (size_t y = 0; y < base->value[x].value->count; y++) {
									sscanf(base->value[x].value->value[y].declaration, "%d", &index);
									cube = dupOBJ(&elements->value[index]);

									addOBJ(&copy, cube);
									cube = NULL;
								}
								break;
							}
						}
					}

					// query is the array of groups
					// elements points to the target cubes
					// copy points to the destination of the copied cubes
					for (size_t y = 0; y < query->value[groups->value[x]].count; y++) {
						if (strcmp(query->value[groups->value[x]].value[y].declaration, "\"children\"") == 0) {
							children = query->value[groups->value[x]].value[y].value;

							for (size_t z = 0; z < children->count; z++) {
								sscanf(children->value[z].declaration, "%d", &index);
								cube = dupOBJ(&elements->value[index]);

								addOBJ(&copy, cube);
								cube = NULL;
							}

							break;
						}
					}

					sscanf(groups->item[x], "\"%[^\"]s\"", file_name);
					permutate = malloc(sizeof(ARCHIVE));
					permutate->name = strdup(file_name);
					permutate->tab = printJSON(value);
					permutate->size = strlen(permutate->tab);

					indentJSON(&permutate->tab);

					permutate_destination = localizeFolder(navigator, command, true);
					addFile(&permutate_destination, permutate);
					permutate = NULL;

					freeOBJ(value);

				}

				for (size_t x = 0; x < navigator->count && trim == true; x++) {
					if (strcmp(navigator->content[x].name, file->name) == 0) {
						delFile(&navigator, x);
						break;
					}
				}

			} else if (strcmp(command, "paint") == 0) {
				int width, height;
				int lines, collums;
				png_bytep *texture, *pixels, *pallet;

				texture = processPNG(file->tab, file->size);
				getPNGDimentions(file->tab, file->size, &height, &width);

				// Map
				checkpoint = strchr(checkpoint, '\"');
				sscanf(checkpoint + 1, "%[^\"]127", name);

				for (size_t x = 0; x < navigator->count; x++) {
					if (strcmp(navigator->content[x].name, name) == 0) {
						pixels = processPNG(navigator->content[x].tab, navigator->content[x].size);
						getPNGDimentions(navigator->content[x].tab, navigator->content[x].size, &lines, &collums);
						break;
					}
				}

				// Pallet
				checkpoint += strlen(name) + 2;
				checkpoint = strchr(checkpoint, '\"');
				sscanf(checkpoint + 1, "%[^\"]127", name);

				for (size_t x = 0; x < navigator->count; x++) {
					if (strcmp(navigator->content[x].name, name) == 0) {
						pallet = processPNG(navigator->content[x].tab, navigator->content[x].size);
						break;
					}
				}

				for (int x = 0; x < height; x++) {

					for (int y = 0; y < width; y++) {
						png_bytep px, compare;
						compare = &texture[x][y * 4];

						for (int z = 0; z < collums && compare[3] != 0; z++) {
							px = &pixels[0][z * 4];

							if (
								px[0] == compare[0]
								&& px[1] == compare[1]
								&& px[2] == compare[2]
							) {
								texture[x][(y * 4) + 0] = pallet[0][(z * 4) + 0];
								texture[x][(y * 4) + 1] = pallet[0][(z * 4) + 1];
								texture[x][(y * 4) + 2] = pallet[0][(z * 4) + 2];
								texture[x][(y * 4) + 3] = pallet[0][(z * 4) + 3];

								break;
							}
						}
					}
				}

				printToPNGFile(&file, texture);

				free(pallet);
				free(texture);
				free(pixels);
			} else if (strcmp(command, "permutate_texture") == 0) {
				int width, height, texture_count = 0;
				int lines, collums;
				ARCHIVE *map, *copies[32];
				FOLDER* location;
				OBJECT* list;
				png_bytep *texture, *pixels, *pallet[32], *to_paint[32];
				
				//Getting texture map name
				checkpoint = strchr(checkpoint, '\"');
				sscanf(checkpoint + 1, "%[^\"]127", name);

				for (size_t x = 0; x < cursor->count; x++) {
					if (strcmp(cursor->content[x].name, name) == 0) {
						map = &cursor->content[x];
						break;
					}
				}

				//Getting pallets list
				checkpoint = strchr(checkpoint, '{');
				sscanf(checkpoint, "%[^\n]255", namespace);
				char* temp = strdup(namespace);
				list = processOBJ(temp);
				free(temp);

				for (size_t x = 0; x < list->count; x++) {
					char* name_pointer;
					sscanf(list->value[x].declaration, "\"%[^\"]s\"", namespace);

					if (namespace[0] == '.') {
						location = localizeFolder(target, namespace, false);
					} else {
						location = localizeFolder(assets, namespace, false);
					}

					if (location == NULL) {
						logger("%s is an invalid path\n", namespace);
						continue;
					}

					if ((name_pointer = strrchr(namespace, '/')) == NULL) {
						name_pointer = namespace;
					} else {
						memmove(namespace, name_pointer + 1, strlen(name_pointer + 1) + 1);
					}

					for (size_t y = 0; y < cursor->count; y++) {

						if (strcmp(location->content[y].name, namespace) == 0) {
							copies[texture_count] = dupFile(file);

							pallet[texture_count] = processPNG(location->content[y].tab, location->content[y].size);
							to_paint[texture_count] = processPNG(copies[texture_count]->tab, copies[texture_count]->size);

							texture_count++;
							break;
						}
					}
				}
				
				//Continue with color substitution
				pixels = processPNG(map->tab, map->size); // Color map
				texture = processPNG(file->tab, file->size); // Texture

				getPNGDimentions(file->tab, file->size, &height, &width);
				getPNGDimentions(map->tab, map->size, &lines, &collums);

				for (int x = 0; x < height; x++) {

					for (int y = 0; y < width; y++) {
						png_bytep px, compare;
						compare = &texture[x][y * 4];

						for (int z = 0; z < collums && compare[3] != 0; z++) {
							px = &pixels[0][z * 4];

							if (
								px[0] == compare[0]
								&& px[1] == compare[1]
								&& px[2] == compare[2]
							) {
								for (int a = 0; a < texture_count; a++) {
									to_paint[a][x][(y * 4) + 0] = pallet[a][0][(z * 4) + 0];
									to_paint[a][x][(y * 4) + 1] = pallet[a][0][(z * 4) + 1];
									to_paint[a][x][(y * 4) + 2] = pallet[a][0][(z * 4) + 2];
									to_paint[a][x][(y * 4) + 3] = pallet[a][0][(z * 4) + 3];
								}

								break;
							}
						}
					}
				}

				free(pixels);
				free(texture);
				freeOBJ(list);

				for (int x = 0; x < texture_count; x++) {
					printToPNGFile(&copies[x], to_paint[x]);
					addFile(&cursor, copies[x]);

					free(to_paint[x]);
					free(pallet[x]);
				}
			} else if (strcmp(command, "convert_overrides") == 0) {
				// (send adress of FOLDER and ARCHIVE pointer, return the ARCHIVE pointer file if 1, return null if 0. Crop overrides of the file if 1, delete components file if 0);
				overridesFormatConvert(navigator, &file);
			}
		}

		if (strstr(file->name, ".json") != NULL) {
			free(file->name);
			file->name = printJSON(placeholder);
		}
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
    lang = getFolder(path, -1);
    returnString(&path, "path");
    returnString(&path, "resourcepacks");

    FOLDER* targets = createFolder(NULL, "targets");
    targets->subdir = (FOLDER*)calloc(2, sizeof(FOLDER));

    //Getting lang file
    translated = getLang(lang, 0);
        
    //Starting the menu;
    initscr();
    setlocale(LC_ALL|~LC_NUMERIC, "");
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
                            temp = getFolder(path, query->value[x]);
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
                                executeInstruct(&targets->subdir[0], &targets->subdir[x + 1], NULL);
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