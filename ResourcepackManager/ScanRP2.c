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
    struct OBJECT** value;
    struct OBJECT* parent;
    size_t count;
    size_t capacity;
    bool indent;
} OBJECT;

typedef struct TEXTURE {
	char *data;
    size_t size;
    size_t offset;  
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
char* display;
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

	if (strlen(path) > 0) {
		temp = realloc(path, (strlen(path) + 1) * sizeof(char));
		if (temp != NULL) {
			path = temp;
			temp = NULL;
		} else {
			logger("Failed to reallocate path string when trimming the path\n");
			return NULL;
		}
	} else {
		free(path);
		path = NULL;
	}
	endQueue(list);

    return path;
}

void printLog(char* path) {
    char date[1024];
	sprintf(date, "%s\\log", path);
	DIR* scan = opendir(path);
	if (scan == NULL) {
		logger("log folder it's missing! Recreating\n");
		mkdir(date);
	}

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

char* strnotchr(const char* str, int count, ...) {
    char* chars = (char*)calloc(count, sizeof(char));
    va_list list;
    va_start(list, count);
    bool found = false;

	if (str == NULL) {
		return NULL;
	}

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

OBJECT* createOBJ (char* key) {
    OBJECT* file = (OBJECT*)malloc(sizeof(OBJECT));
    file->capacity = 8;
    file->count = 0;
    file->declaration = strdup(key);
    file->parent = NULL;
    file->value = NULL;
    file->indent = false;

    return file;
}

void freeOBJ (OBJECT **file) {
	if (*file == NULL) {
		return;
	}

	OBJECT *pointer = *file;
	if (pointer->declaration != NULL) {
		free(pointer->declaration);
		pointer->declaration = NULL;
	}

	while (pointer->count > 0) {
		OBJECT *temp = pointer->value[pointer->count - 1];
		freeOBJ(&temp);
		pointer->count--;
	}

	if (pointer->value != NULL) {
		free(pointer->value);
		pointer->value = NULL;
	}
}

void addOBJ (OBJECT* file, OBJECT** value) {
	if (file->count == 0) {
		OBJECT **temp = (OBJECT**)calloc(file->capacity, sizeof(OBJECT*));
		
		if (temp == NULL) {
			logger("addOBJ: Error! Failed to alloc memory for struct, <%s>!\n", strerror(errno));
			return;
		}
		file->value = temp;
	}
	if (file->count == file->capacity) {
		file->capacity += 8;
		OBJECT **temp = (OBJECT**)realloc(file->value, file->capacity * sizeof(OBJECT*));

		if (temp == NULL) {
			logger("addOBJ: Error! Failed to alloc memory for struct, <%s>!\n", strerror(errno));
			return;
		}
		file->value = temp;
	}
	value[0]->parent = file;
	file->value[file->count] = *value;
	file->count++;
}

void delOBJ (OBJECT* file, size_t key) {
	freeOBJ(&file->value[key]);

	for (size_t x = key; x < (file->count - 1); x++) {
		file->value[x] = file->value[x + 1];
	}

	if (file->count <= file->capacity / 2) {
		file->capacity /= 2;

		OBJECT** temp = (OBJECT**)realloc(file->value, file->capacity * sizeof(OBJECT*));
		if (temp == NULL) {
			logger("addOBJ: Error! Failed to trim memory for struct, <%s>!\n", strerror(errno));
			return;
		}
		file->value = temp;
	}
}

OBJECT* dupOBJ (OBJECT* target) {
    OBJECT* mirror = createOBJ(target->declaration);
	mirror->indent = target->indent;

	for (size_t x = 0; x < target->count; x++) {
		OBJECT* pointer = target->value[x];
		addOBJ(mirror, &pointer);
		pointer = NULL;
	}

	return mirror;
}

char* strchrs (const char* str, int count, ...) {
	if (str == NULL) {
		return NULL;
	}
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

OBJECT* processJSON (char* json) {
	char *pointer, *checkpoint, *buffer, type[2];
	OBJECT *file, *value;
	size_t length, size = 1024;
	file = value = NULL;

	buffer = (char*)calloc(size, sizeof(char));

	type[0] = type[1] = ' ';
	for (size_t x = 0; x < strlen(json); x++, value = NULL) {
		pointer = strnotchr((json + x), 3, ' ', '\t', ',');
		if (pointer == NULL) {
			break;
		}
		x += (pointer - (json + x));

		switch (json[x])
		{
		case '}':
			/*fallthrough*/
		case ']':
			file = (file->parent != NULL) ? file->parent : file;
			break;
		case '{':
			type[0] = '{';
			type[1] = '}';
			/*fallthrough*/
		case '[':
			if (type[0] == ' ') {
				type[0] = '[';
				type[1] = ']';
			}

			if (type[0] == '{') {
				value = createOBJ("obj");
			} else {
				value = createOBJ("array");
			}

			if (file == NULL) {
				file = value;
			} else {
				addOBJ(file, &value);
				file = value;
			}
			value = NULL;

			type[0] = type[1] = ' ';
			break;
		case '\n':
			file->indent = true;
			break;
		case '\"':
			pointer = strchr(&json[x + 1], '\"');
			length = (pointer - (json + x)) + 1;

			if (length > size) {
				size += length;

				char* temp = (char*)realloc(buffer, size * sizeof(char));
				if (temp == NULL) {
					logger("Error! Failed to resize buffer for json process! %s\n", strerror(errno));
					free(buffer);
					break;
				}
				buffer = temp;
				temp = NULL;
			}

			snprintf(buffer, size - length, "%.*s", (int)length, (json + x));
			value = createOBJ(buffer);
			addOBJ(file, &value);
			x += length - 1;

			break;
		case ':':
			pointer = strnotchr(&json[x], 4, ':', ' ', '\n', '\t');
			x += (int)(pointer - (json + x));

			if (pointer[0] == '{' || pointer[0] == '[') {
				type[0] = pointer[0];
				type[1] = (pointer[0] == '{') ? '}' : ']';

				for (size_t a = x + 1, b = 1; a < strlen(json); a++) {
					if (json[a] == type[0]) {
						b++;
					} else if (json[a] == type[1]) {
						b--;
					}
					if (b == 0) {
						pointer = &json[a + 1];
						break;
					}
				}

				length = (pointer - (json + x));

				if (length > (size - 1)) {
					size += length;
	
					char* temp = (char*)realloc(buffer, size * sizeof(char));
					if (temp == NULL) {
						logger("Error! Failed to resize buffer for json process! %s\n", strerror(errno));
						free(buffer);
						break;
					}
					buffer = temp;
					temp = NULL;
				}

				sprintf(buffer, "%.*s", (int)length, (json + x));
				value = processJSON(buffer);

				x += length + 1;
			} else {
				if (pointer[0] == '\"') {
					checkpoint = strchr(pointer + 1, '\"');
					checkpoint++;
				} else {
					checkpoint = strchrs(pointer + 1, 5, '}', ']', ' ', ',', '\n');
				}
				length = (checkpoint - pointer);

				if (length > (size - 1)) {
					size += length;
	
					char* temp = (char*)realloc(buffer, size * sizeof(char));
					if (temp == NULL) {
						logger("Error! Failed to resize buffer for json process! %s\n", strerror(errno));
						free(buffer);
						break;
					}
					buffer = temp;
					temp = NULL;
				}

				sprintf(buffer, "%.*s", (int)length, pointer);
				value = createOBJ(buffer);

				x = (checkpoint - json) + 1;
			}
			addOBJ(file->value[file->count - 1], &value);

			break;
		default: // Issue getting commas at end
			pointer = strchrs((json + x), 5, ' ', ',', '\n', '}', ']');
			if (pointer == NULL) {
				length = strlen(json + x);
			} else {
				length = (pointer - (json + x));
			}

			snprintf(buffer, size, "%.*s", (int)length, (json + x));
			value = createOBJ(buffer);
			addOBJ(file, &value);
			value = NULL;
			x += strlen(buffer);
			break;
		}
	}

	free(buffer);
	return file;
}

void indentJSON (char** file) {
    char *pointer = *file, *temp;
    size_t count = 0;

    while ((pointer = strchr(pointer + 1, '\n')) != NULL) {
        count++;
    }

    if (count == 0) {
        return;
    }

    temp = (char*)realloc(*file, (strlen(*file) + count)* sizeof(char));
    if (temp != NULL) {
        *file = temp;
        temp = NULL;
    } else {
        logger("Error! Couldn't expand string size for indentation, %s\n", strerror(errno));
		return;
    }

    for (
		pointer = strchr(*file, '\n');
		pointer != NULL && count > 1;
		pointer = strnotchr(pointer + 1, 1, '\t'), pointer = strchr(pointer, '\n'), count--
	) {
        memmove(pointer + 1, pointer, strlen(pointer) + 1);
        pointer[1] = '\t';
    }
}

char* printJSON (OBJECT* json) {
	char *file, *placeholder;
    size_t length = 0, size = 1024;
    OBJECT* navigator = json;
    file = (char*)calloc(size, sizeof(char));
    *file = '\0';

	if (strcmp(navigator->declaration, "obj") == 0 || strcmp(navigator->declaration, "array") == 0) {
		snprintf(
			file,
			1024,
			"%c%s",
			(strcmp(navigator->declaration, "obj") == 0) ? '{' : '[',
			(navigator->indent == true) ? "\n" : ""
		);
		
		for (size_t x = 0; x < navigator->count; x++) {
			placeholder = printJSON(navigator->value[x]);

			if (navigator->indent == true) {
				indentJSON(&placeholder);
			}
			
			length += snprintf (
				NULL,
				0,
				"%s%s",
				placeholder,
				(x == (navigator->count - 1))
					? (navigator->indent == true)
						? "\n"
						: ""
					: (navigator->indent == true)
						? ",\n"
						: ", "
			);
			
			if (x == (navigator->count - 1)) {
				length++;
			}

			if (strlen(file) + length >= (size - 1)) {
				size += length;
				char* temp = (char*)realloc(file, size * sizeof(char));

				if (temp == NULL) {
					logger("Error resizing array while printing json! %s\n", strerror(errno));
					free(placeholder);
					continue;
				} else {
					file = temp;
					temp = NULL;
				}
			}

			snprintf (
				file + strlen(file),
				size - strlen(file),
				"%s%s",
				placeholder,
				(x == (navigator->count - 1))
					? (navigator->indent == true)
						? "\n"
						: ""
					: (navigator->indent == true)
						? ",\n"
						: ", "
			);

			if (x == (navigator->count - 1)) {
				sprintf(file + strlen(file), "%c", (strcmp(navigator->declaration, "obj") == 0) ? '}' : ']');
			}

			free(placeholder);
		}
	} else {
		sprintf(file, "%s", navigator->declaration);
	
		for (size_t x = 0; x < navigator->count; x++) {
			if (strcmp(navigator->value[x]->declaration, "obj") == 0 || strcmp(navigator->value[x]->declaration, "array") == 0) {
				placeholder = printJSON(navigator->value[x]);
				length += strlen(placeholder);
			} else {
				placeholder = strdup(navigator->value[x]->declaration);
			}
		
			if (strlen(file) + length >= (size - 1)) {
				size += length + 1024;
				char* temp = (char*)realloc(file, size * sizeof(char));
		
				if (temp == NULL) {
					logger("Error resizing array while printing json! %s\n", strerror(errno));
					free(placeholder);
					return NULL;
				} else {
					file = temp;
					temp = NULL;
				}
			}

			sprintf(file + strlen(file), ": %s", placeholder);
			free(placeholder);
		}
	}

	char* temp = (char*)realloc(file, (strlen(file) + 1) * sizeof(char));
	if (temp == NULL) {
		logger("Error resizing array while printing json! %s\n", strerror(errno));
		return NULL;
	} else {
		file = temp;
		temp = NULL;
	}

	return file;
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
		FOLDER* old = parent[0]->subdir;
		size_t newCapacity = parent[0]->capacity * 2;
        
        FOLDER* temp = (FOLDER*)realloc(parent[0]->subdir, newCapacity * sizeof(FOLDER));
        if (temp == NULL) {
            logger("Error reallocating memory for new folder, %s\n", strerror(errno));
            return;
        }

        parent[0]->subdir = temp;
        parent[0]->capacity = newCapacity;

		if (parent[0]->subdir != old) {
			for (int x = 0; x < (int)parent[0]->subcount; x++) {
				parent[0]->subdir[x].parent = parent[0];
			}
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

    placeholder = (char*)realloc(buffer, (length + 1) * sizeof(char));
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
			return;
        }
    }
    if (folder[0]->count >= (folder[0]->file_capacity - 1)) {
        folder[0]->file_capacity *= 2;
        ARCHIVE* temp = (ARCHIVE*)realloc(folder[0]->content, folder[0]->file_capacity * sizeof(ARCHIVE));

        if (temp == NULL) {
            logger("Error reallocating memory for new file, %s\n", strerror(errno));
			return;
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
        addFile(&dup, mirror);
        mirror = NULL;
    }

    for (int x = 0; x < (int)base->subcount; x++) {
        FOLDER* mirror = dupFolder(&base->subdir[x]);
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

void refreshWindows() {
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

//Prepares the confirmation dialog screen with the gives line from lang file.
//Type 0 will prepare a yes/no question. Type 1 will prepare a notice
bool confirmationDialog(char* lang, int line, int* sizes, int type) {
    size_t center, size;
	char options[4][128];
	int choice, key;
    char *pointer = lang, *checkpoint;
	key = choice = 0;
	bool update = true;

	wclear(action);
    box(action, 0, 0);
    center = getmaxx(action);

    for (int x = 1; x < line; x++, pointer += 1) {
        pointer = strchr(pointer, '\n');
    }
    checkpoint = pointer;
    pointer = strchr(pointer, '\n');
    pointer += 1;
    size = pointer - checkpoint - 1;
	sprintf(options[0], "%.*s", (int)size, checkpoint);

	pointer = lang;
	for (int x = 0; x < 6; x++, pointer++) {
		pointer = strchr(pointer, '\n');
	}

	//Get Lang "YES"
	checkpoint = pointer;
	pointer = strchr(pointer, '\n');
	pointer += 1;
	size = pointer - checkpoint - 1;
	sprintf(options[1], "%.*s", (int)size, checkpoint);
	sizes[0] = size;

	//Get Lang "NO"
	checkpoint = pointer;
	pointer = strchr(pointer, '\n');
	pointer += 1;
	size = pointer - checkpoint - 1;
	sprintf(options[2], "%.*s", (int)size, checkpoint);
	sizes[1] = size;

	//Get Lang "GOT IT"
	checkpoint = pointer;
	pointer = strchr(pointer, '\n');
	pointer += 1;
	size = pointer - checkpoint - 1;
	sprintf(options[3], "%.*s", (int)size, checkpoint);

	while (true) {
		if (update) {
			wclear(action);
			box(action, 0, 0);

			if (strlen(options[0]) > center - 2) {
				for (int x = center - 2; x > 0 && *pointer != ' '; x--) {
					pointer = &options[0][x];
				}
				size = (pointer - options[0]);
				mvwprintw(action, 1, ((center - size)/2), "%.*s", (int)size, options[0]);
				mvwprintw(action, 2, ((center - (strlen(pointer) - 1))/2), "%.*s", strlen(pointer), pointer);
			} else {
				mvwprintw(action, 1, ((center - strlen(options[0]))/2), "%s", options[0]);
			}

			if (type == 1) {
				mvwprintw(action, getmaxy(action) - 2, ((center - strlen(options[3]))/2), "%s", options[3]);
			} else {
				mvwprintw(action, getmaxy(action) - 2, ((center - sizes[0])/4), "%s", options[1]);
				mvwprintw(action, getmaxy(action) - 2, ((center - sizes[1])*3/4), "%s", options[2]);
			}
			wrefresh(action);

			update = false;
		}

		if (type == 1) {
			mvwchgat(action, getmaxy(action) - 2, ((center - strlen(options[3]))/2), strlen(options[3]), A_STANDOUT, 0, NULL);
		} else {
			if (choice == 0) {
				mvwchgat(action, getmaxy(action) - 2, ((center - sizes[0])/4), sizes[0], A_STANDOUT, 0, NULL);
			} else {
				mvwchgat(action, getmaxy(action) - 2, ((center - sizes[1])*3/4), sizes[1], A_STANDOUT, 0, NULL);
			}
		}
		wrefresh(action);

		key = wgetch(window);
		switch (key)
		{
		case ENTER:
			if (choice == 0) {
				wclear(action);
				wrefresh(action);

				refreshWindows();
				return true;
			} else {
				wclear(action);
				wrefresh(action);

				refreshWindows();
				return false;
			}
			break;
		case KEY_LEFT:
			if (choice > 0) {
				choice--;
			}
			break;
		case KEY_RIGHT:
			if (choice < 1) {
				choice++;
			}
			break;
		case KEY_RESIZE:
			refreshWindows();

			update = true;
			break;
		default:
			break;
		}

		if (type == 1) {
			mvwchgat(action, getmaxy(action) - 2, ((center - strlen(options[3]))/2), strlen(options[3]), A_NORMAL, 0, NULL);
		} else {
			if (choice == 0) {
				mvwchgat(action, getmaxy(action) - 2, ((center - sizes[1])*3/4), sizes[1], A_NORMAL, 0, NULL);
			} else {
				mvwchgat(action, getmaxy(action) - 2, ((center - sizes[0])/4), sizes[0], A_NORMAL, 0, NULL);
			}
		}
	}
}

//Read from a target file into the memory
//pack is the parent folder, path is the path to loop for and position is the file position. -1 Will take the path as target.
FOLDER* getFolder(char* path, int position) {
	int dirNumber = 0, dirPosition[16], input, pin = report_end;
	struct dirent *entry;
	char namespace[512], name[256], *pointer;
	FOLDER *folder, *navigator;
	DIR* scanner;
	bool result = false;
	
	if (translated != NULL) {
		refreshWindows();
        nodelay(window, true);
    }
	if ((scanner = opendir(path)) == NULL) {
		logger("Error! Failed to open <%s>: %s\n", path, strerror(errno));
		return NULL;
	}

	sprintf(namespace, "%s", path);
	if (position != -1) {
		// Openning target folder
		seekdir(scanner, position + 2);
		if ((entry = readdir(scanner)) == NULL) {
			logger("Error! Failed to read <%s>: %s\n", path, strerror(errno));
			return NULL;
		}

		snprintf(namespace + strlen(namespace), 512 - strlen(namespace), "\\%s", entry->d_name);
		closedir(scanner);
	}

	struct stat fileStat;
	if (stat(namespace, &fileStat) == 0) {
		if (S_ISDIR(fileStat.st_mode)) {
			input = 0;
		} else if (S_ISREG(fileStat.st_mode)) {
			if (strstr(namespace, ".zip") != NULL) {
				input = 2;
			} else {
				input = 1;
			}
		}
	} else {
		logger("Error! Failed to get entry stat! <%s>: %s\n", namespace, strerror(errno));
		return NULL;
	}
	if (input == 0) {
		if ((scanner = opendir(namespace)) == NULL) {
			logger("Error! Failed to open <%s>: %s\n", path, strerror(errno));
			return NULL;
		}
	
		seekdir(scanner, 2);
		entry = readdir(scanner);
	}

	for (int x = 0; x < 16; x++) {
		dirPosition[x] = 0;
	}

	pointer = strrchr(namespace, '\\');
	sprintf(name, "%s", pointer + 1);
	folder = createFolder(NULL, name);

	navigator = folder;

	nodelay(window, true);
	// Test with a regular resourcepack
	// Create folder tree printing method (for fun)
	if (input == 0) { // Dir
		logger("FOLDER: <%s> is a regular directory\n", navigator->name);
		pointer = &namespace[strlen(namespace)];
		
		while (!result) {
			input = wgetch(window);
			wclear(miniwin);

			if (input == KEY_RESIZE) {
				refreshWindows();
			}

			input = (report_end - pin) > (size_t)(getmaxy(miniwin) - 2) ? (int)(report_end - (getmaxy(miniwin) - 2)) : pin;
			mvwprintLines(miniwin, report, 1, 1, input, report_end);

			box(miniwin, 0, 0);
			wrefresh(miniwin);

			sprintf(pointer, "\\%s", entry->d_name);

			if (stat(namespace, &fileStat) == 0) {
				if (S_ISDIR(fileStat.st_mode)) {
					FOLDER* mirror = createFolder(NULL, pointer + 1);
					addFolder(&navigator, mirror);

					logger("FOLDER: <%s>\n", namespace);
					mirror = NULL;
				} else if (S_ISREG(fileStat.st_mode)) {
					ARCHIVE* mirror = getFile(namespace);
					addFile(&navigator, mirror);

					logger("FILE: <%s>\n", namespace);
					mirror = NULL;
				}
			}

			pointer[0] = '\0';

			if ((entry = readdir(scanner)) == NULL) {
				while (dirPosition[dirNumber] == (int)navigator->subcount && dirNumber > -1) {
					if ((pointer = strrchr(namespace, '\\')) != NULL) {
						pointer[0] = '\0';
					} else {
						namespace[0] = '\0';
					}
		
					dirPosition[dirNumber] = 0;
					dirNumber--;
					navigator = (navigator->parent != NULL) ? navigator->parent : navigator;
				}

				if (dirNumber == -1) {
					result = true;
					break;
				}

				navigator = &navigator->subdir[dirPosition[dirNumber]];

				sprintf(pointer, "\\%s", navigator->name);
				pointer = &namespace[strlen(namespace)];

				closedir(scanner);
				if ((scanner = opendir(namespace)) != NULL) {
					seekdir(scanner, 2);
					entry = readdir(scanner);

					dirPosition[dirNumber]++;
                    dirNumber++;
				} else {
					logger("Error! Failed to open <%s>, %s\n", namespace, strerror(errno));
					dirPosition[dirNumber] = 0;
					dirNumber--;
					navigator = navigator->parent;
				}
			}
		}

	} else if (input == 2) { // Zip
		logger("FOLDER: <%s> is a regular zip file\n", navigator->name);
		char placeholder[512];
		result = true;

        unzFile rp = unzOpen(namespace);
        if (rp == NULL) {
            logger("Error! Could not open zip folder, %s\n", folder->name, strerror(errno));

            wrefresh(miniwin);
            unzClose(rp);
            return NULL;
        }

        if (unzGoToFirstFile(rp) != UNZ_OK) {
            logger("Error! Could not go to first file, %s\n", folder->name, strerror(errno));

            wrefresh(miniwin);
            unzClose(rp);
            return NULL;
        }

        unz_file_info zip_entry;
		while (result) {
			input = wgetch(window);
			wclear(miniwin);

			if (input == KEY_RESIZE) {
				refreshWindows();
			}

			input = (report_end - pin) > (size_t)(getmaxy(miniwin) - 2) ? (int)(report_end - (getmaxy(miniwin) - 2)) : pin;
			mvwprintLines(miniwin, report, 1, 1, input, report_end);

			box(miniwin, 0, 0);
			wrefresh(miniwin);

			input = unzGetCurrentFileInfo(rp, &zip_entry, namespace, sizeof(namespace), NULL, 0, NULL, 0);

            if (input != UNZ_OK) {
                logger("Error! Could not get file info, %s\n", name, strerror(errno));
				freeFolder(folder);
                return NULL;
            }

			sprintf(placeholder, namespace);
			if (placeholder[strlen(placeholder) - 1] == '/') {
				placeholder[strlen(placeholder) - 1] = '\0';
				input = 0;
			} else {
				input = 1;
			}
			if ((pointer = strrchr(placeholder, '/')) != NULL) {
				pointer[0] = '\0';
			}
			if ((pointer = strrchr(placeholder, '/')) == NULL) {
				pointer = &placeholder[0];
			} else {
				pointer++;
			}

			while (strcmp(navigator->name, pointer) != 0 && dirNumber > 0) {
				navigator = (navigator->parent != NULL) ? navigator->parent : navigator;

				logger("FOLDER: Return to <%s>\n", navigator->name);
				dirNumber--;
			}

			if (input == 0) { // Folder
				sprintf(placeholder, namespace);
				if ((pointer = strrchr(placeholder, '/')) != NULL) {
					pointer[0] = '\0';
				}
				if ((pointer = strrchr(placeholder, '/')) == NULL) {
					pointer = &placeholder[0];
				} else {
					pointer++;
				}

				FOLDER* mirror = createFolder(NULL, pointer);
				addFolder(&navigator, mirror);
				navigator = &navigator->subdir[navigator->subcount - 1];

				dirNumber++;

				logger("FOLDER: <%s>\n", namespace);
				mirror = NULL;
			} else if (input == 1) { // File
				ARCHIVE* mirror = getUnzip(rp, namespace);
                addFile(&navigator, mirror);

				logger("FOLDER: <%s>\n", namespace);
				mirror = NULL;
			}

			input = unzGoToNextFile(rp);
			result = (input == UNZ_OK) ? true : false;
		}
	} else {
		free(folder);
		folder = NULL;	
	}

	nodelay(window, false);
	return folder;
}

FOLDER* localizeFolder(FOLDER* folder, char* path, bool recreate_path) {
    char dir[16][256], *pointer, *checkpoint;
    FOLDER* navigator = folder;
	int dirNumber = 0;

	if (folder == NULL) {
		logger("Warning! FOLDER passed is NULL!\n");
		return NULL;
	}

	if ((pointer = strchr(path, ':')) != NULL) {
		sprintf(dir[0], "assets");
		sprintf(dir[1], "%.*s", (int)(pointer - path), path);
		pointer++;
		dirNumber = 2;
	} else if (strstr(path, "./") != NULL) {
		pointer = checkpoint = path + 2;
	} else {
		pointer = checkpoint = path;
	}
	for (int x = dirNumber; (checkpoint = strchr(pointer, '/')) != NULL && x < 16; x++) {
		sprintf(dir[x], "%.*s", (int)(checkpoint - pointer), pointer);
		pointer = checkpoint + 1;
		dirNumber++;
	}

	for (int x = 0, y = 0; y < dirNumber && x < (int)(navigator->subcount + 1); x++) {
		if (x < (int)navigator->subcount && strcmp(navigator->subdir[x].name, dir[y]) == 0) {
			navigator = &navigator->subdir[x];

			y++;
			x = -1;
		} else if (x == (int)navigator->subcount && recreate_path) {
			logger("FOLDER \"%s\" was created\n", dir[y]);
			FOLDER* temp = createFolder(NULL, dir[y]);
			addFolder(&navigator, temp);
			temp = NULL;
			navigator = &navigator->subdir[x];

			y++;
			x = -1;
		} else if (x == (int)navigator->subcount && !recreate_path) {
			logger("FOLDER %s <%s> wasn't found!\n", dir[y], path);
			break;
		}
	}

    return navigator;
}

void copyOverides (ARCHIVE* file, ARCHIVE* overrides) {
	/* 
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
	*/
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
            refreshWindows();
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
	nodelay(window, false);
    logger("Finished targets linking, starting the override process\n");
}

void decodePNG (png_structp png_ptr, png_bytep data, png_size_t size) {
	TEXTURE* reader = (TEXTURE*)png_get_io_ptr(png_ptr);

	if (reader->offset + size > reader->size) {
        logger("Read error: beyond buffer size!\n");
    }
	memcpy(data, reader->data + reader->offset, size);
	reader->offset += size;
}

void encodePNG (png_structp png_ptr, png_bytep data, png_size_t size) {
	TEXTURE* reader = (TEXTURE*)png_get_io_ptr(png_ptr);

	if (reader->offset + size > reader->size) {
		size_t new_size = reader->size + 256;
		char* temp = (char*)realloc(reader->data, new_size);

		if (temp == NULL) {
			logger("Failed to expand pixels memory! %s\n", strerror(errno));
			return;
		}

		reader->data = temp;
		reader->size = new_size;
    }
	
	memcpy(reader->data + reader->offset, data, size);
	reader->offset += size;
}

int getPNGPixels(ARCHIVE* file, png_bytep** pixels, int* width, int* height, int* color_type, int* bit_depth, int* row_bytes, bool extract) {
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL) {
		logger("Failed to create read struct for %s\n", file->name);
		return 0;
	}

	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		logger("Failed to create info struct for %s\n", file->name);
		png_destroy_read_struct(&png, NULL, NULL);
		return 0;
	}

	if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        return 0;
    }

	TEXTURE reader = {(char*)file->tab, file->size, 0};
	png_set_read_fn(png, &reader, decodePNG);
	png_read_info(png, info);

	if (height != NULL) {
		*height = png_get_image_height(png, info);
	}
	if (width != NULL) {
		*width = png_get_image_width(png, info);
	}
	if (bit_depth != NULL) {
		*bit_depth = png_get_bit_depth(png, info);
	}
	if (color_type != NULL) {
		*color_type = png_get_color_type(png, info);
	}
	if (row_bytes != NULL) {
		*row_bytes = png_get_rowbytes(png, info);
	}
	if (extract) {
		size_t byte_size = png_get_rowbytes(png, info);
		size_t lines;
		lines = png_get_image_height(png, info);
		png_bytep* pxs = (png_bytep*)malloc(lines * sizeof(png_bytep));

		for (size_t x = 0; x < lines; x++) {
			pxs[x] = (png_bytep)malloc(byte_size);

			if (pxs[x] == NULL) {
				logger("Failed to allocate memory for row of png bytes for %s! %s\n", file->name, strerror(errno));

				for (size_t y = 0; y < x; y++) {
					free(pxs[y]);
				}
				free(pxs);
				png_destroy_read_struct(&png, &info, NULL);
				*pixels = NULL;
            	return 0;
			}
		}

		png_read_image(png, pxs);
		*pixels = pxs;
	}
	png_destroy_read_struct(&png, &info, NULL);

	return 1;
}

void printPNGPixels(ARCHIVE* file, png_bytep *pixels, int width, int height) {
	// Creating writing structs
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (png == NULL) {
		logger("Failed to create read struct for %s\n", file->name);
		return;
	}

	png_infop info = png_create_info_struct(png);
	if (info == NULL) {
		logger("Failed to create info struct for %s\n", file->name);
		png_destroy_write_struct(&png, NULL);
		return;
	}

	// Error handling
	if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        return;
    }

	int color_type, bit_depth;
	getPNGPixels(file, NULL, NULL, NULL, &color_type, &bit_depth, NULL, false);
	
	TEXTURE texture;
    texture.data = (char*)malloc(file->size);
    texture.size = file->size;
    texture.offset = 0;
	if (texture.data == NULL) {
		logger("Failed to allocate memory for texture TEXTURE %s\n", strerror(errno));
		png_destroy_write_struct(&png, &info);
		return;
	}

	png_set_write_fn(png, &texture, encodePNG, NULL);
	
	png_set_IHDR(
		png, info,
		width, height,
		bit_depth, color_type,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT
	);

	png_set_compression_level(png, Z_BEST_COMPRESSION);

	png_write_info(png, info);
	png_write_image(png, pixels);
	png_write_end(png, NULL);

	logger("Painted PNG %s data size %zu\n", file->name, texture.size);

	if (file->tab != NULL) {
		free(file->tab);
	}
	file->tab = texture.data;
	file->size = texture.offset;

	png_destroy_write_struct(&png, &info);
} 

void resizePNGFile(ARCHIVE** image, int width, int height) {
	int collums, lines, color_type, bit_deph, row_bytes, offset;
	png_bytep *texture;

	getPNGPixels(*image, &texture, &collums, &lines, &color_type, &bit_deph, &row_bytes, true);
	offset = (color_type == PNG_COLOR_TYPE_RGBA) ? 4 : 3;

	png_bytep *resize = (png_bytep*)realloc(texture, height * sizeof(png_bytep));
	if (resize == NULL) {
		logger("Error! Failed to resizer %s! %s\n", image[0]->name, strerror(errno));
		return;
	}

	// Allocating lines memory in case the new size it's bigger

	for (int x = 0; x < height; x++) {
		if (x < lines) {
			resize[x] = (png_bytep)realloc(resize[x], row_bytes);
		} else {
			resize[x] = (png_bytep)malloc(row_bytes);
		}

		if (resize[x] == NULL) {
			logger("Error while resizing png byte rows!\n", strerror(errno));

			for (int a = 0; a < x; a++) {
				free(resize[a]);
			}
			free(resize);
			return;
		}
	}

	if (offset == 3) {
		logger("%s has no alpha channel, filling with white pixels\n", image[0]->name);
	}

	for (int x = 0; x < height; x++) {
		for (int y = 0; y < width; y++) {
			if (x > (lines - 1) || y > (collums - 1)) {
				if (offset == 3) {
					resize[x][(y * offset) + 0] = 255;
					resize[x][(y * offset) + 1] = 255;
					resize[x][(y * offset) + 2] = 255;
				} else {
					resize[x][(y * offset) + 3] = 0;
				}
			}
		}
	}

	printPNGPixels(image[0], resize, width, height);
}

void overridesFormatConvert(FOLDER* folder, ARCHIVE** file) {/* 
	OBJECT *placeholder, *overrides, *query, *value, *model, *temp;
	QUEUE *types, *list;
	FOLDER* location;
	char buffer[1024], *pointer, name[256];
	int type = 0;
	float index = 0;
	bool custom_model_data, crossbow, pulling, damage;
	custom_model_data = crossbow = false;

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
	} else if (strstr(file[0]->name, "compass") != NULL || strstr(file[0]->name, "clock") != NULL) {
		type = 2;
	} else {
		type = 3;
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
				custom_model_data = true;
				break;
			}
		}

		// Sorting models
		for (size_t x = 0; x < overrides->count; x++) {
			index = -1;
			pulling = false;

			for (size_t y = 0; y < overrides->value[x].count; y++) {
				if (strcmp(overrides->value[x].value[y].declaration, "\"model\"") == 0) {
					model = overrides->value[x].value[y].value;
				} else if (strcmp(overrides->value[x].value[y].declaration, "\"predicate\"") == 0) {
					query = overrides->value[x].value[y].value;
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
							temp->value[0].value = NULL;
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
			if (value->value != NULL) {
				freeOBJ(&value->value[0]);
				value->count--;
				value->value = NULL;
			}

			addOBJ(&value, temp);
			temp = NULL;
		}

		break;
	case 2:
		OBJECT* cases, *mirror;
		if (strcmp(file[0]->name, "compass.json") == 0) {
			sprintf(buffer, "{\n\t\"type\": \"minecraft:condition\",\n\t\"property\": \"minecraft:has_component\",\n\t\"component\": \"minecraft:lodestone_tracker\",\n\t\"on_true\": {},\n\t\"on_false\": {}\n}");
			temp = processOBJ(buffer);
			freeOBJ(temp->value[3].value);
			freeOBJ(temp->value[4].value);
			temp->value[3].value = NULL;
			temp->value[4].value = NULL;
			temp->value[3].count--;
			temp->value[4].count--;

			addOBJ(&placeholder, temp);
			temp = NULL;

			placeholder = &placeholder->value->value[4]; // "on_false"
		}

		if (strcmp(file[0]->name, "recovery_compass.json") != 0) {
			// "recovery_compass" doesn't have select "context dimention" property
			sprintf(buffer, "{\n\t\"type\": \"minecraft:select\",\n\t\"property\": \"minecraft:context_dimension\",\n\t\"cases\": [\n\t],\n\t\"fallback\": {}\n}");
			temp = processOBJ(buffer);
			free(temp->value[3].value);
			temp->value[3].count--;
			temp->value[3].value = NULL;

			addOBJ(&placeholder, temp);
			placeholder = placeholder->value;
			temp = NULL;
		}

		// create "range_dispatch" on temp pointer		
		sprintf(
			buffer,
			"{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:%s\",\n\t\"entries\": [\n\t]\n}",
			strstr(file[0]->name, "compass")
				? "compass"
				: "time"
		);

		temp = processOBJ(buffer);
		value = &temp->value[2];

		for (size_t x = 0; x < overrides->count; x++) {
			for (size_t y = 0; y < overrides->value[x].count; y++) {
				if (strcmp(overrides->value[x].value[y].declaration, "\"model\"") == 0) {
					model = overrides->value[x].value[y].value;
				} else if (strcmp(overrides->value[x].value[y].declaration, "\"predicate\"") == 0) {
					query = overrides->value[x].value[y].value;
				}
			}

			for (size_t y = 0; y < query->count; y++) {
				if (strcmp(query->value[y].declaration, "\"angle\"")  == 0 || strcmp(query->value[y].declaration, "\"time\"") == 0) {
					index = strtof(query->value[y].value->declaration, NULL);
					break;
				}
			}

			pointer = strrchr(model->declaration, '/');
			sscanf(pointer + 1, "%255[^\"]", name);

			sprintf(buffer, "{\n\t\"model\": {\n\t\t\"type\": \"minecraft:model\"\n\t\t\"model\": \"minecraft:item/%s\"\n\t},\n\t\"threshold\": %f\n}", name, index);
			mirror = processOBJ(buffer);

			addOBJ(&value->value, mirror);
			mirror = NULL;
		}

		// *temp is the cases
		if (strcmp(file[0]->name, "recovery_compass.json") != 0) {
			// adding the target to "range_dispatch"
			if (strcmp(file[0]->name, "compass.json") == 0) {
				sprintf(buffer, " \"target\": \"spawn\"");
			} else {
				sprintf(buffer, " \"source\": \"daytime\"");
			}
			mirror = processOBJ(buffer);
			addOBJ(&temp, mirror->value);
			mirror->value = NULL;
			freeOBJ(mirror);

			// preparing case
			sprintf(buffer, "{\n\t\"model\": {\n\t},\n\t\"when\": \"overworld\"\n}");
			cases = processOBJ(buffer);
			freeOBJ(cases->value[0].value);
			cases->value[0].count--;
			
			// duplicating range_dispatch and adding to model value
			mirror = dupOBJ(temp);
			value = &cases->value[0];
			addOBJ(&value, mirror);
			mirror = NULL;
			value = NULL;

			// adding to "on_false" on select
			addOBJ(&placeholder->value[2].value, cases);
			cases = NULL;

			// preparing fallback
			// adding the target to "range_dispatch"
			if (strcmp(file[0]->name, "compass.json") == 0) {
				sprintf(buffer, " \"target\": \"none\"");
			} else {
				sprintf(buffer, " \"source\": \"random\"");
			}
			delOBJ(&temp, (temp->count - 1));
			mirror = processOBJ(buffer);
			addOBJ(&temp, mirror->value);
			mirror->value = NULL;
			freeOBJ(mirror);

			// duplicating range_dispatch
			mirror = dupOBJ(temp);

			// adding to fallback
			value = &placeholder->value[3]; 
			addOBJ(&value, mirror);
			mirror = NULL;
			value = NULL;

			placeholder = placeholder->parent->parent;
		} else {
			sprintf(buffer, " \"target\": \"recovery\"");
			mirror = processOBJ(buffer);
			addOBJ(&temp, mirror->value);
			addOBJ(&placeholder, temp);

			temp = NULL;
			mirror->value = NULL;
			freeOBJ(mirror);
			mirror = NULL;

			placeholder = placeholder->parent;
		}

		if (strcmp(file[0]->name, "compass.json") == 0) {
			placeholder = placeholder->parent->parent;
			delOBJ(&temp, (temp->count - 1));
			sprintf(buffer, " \"target\": \"lodestone\"");

			mirror = processOBJ(buffer);
			addOBJ(&temp, mirror->value);
			mirror->value = NULL;
			freeOBJ(mirror);
			
			mirror = &placeholder->value[3];
			addOBJ(&mirror, temp);
			mirror = NULL;
		}

		break;
	case 3:
		// It's a regular model

		for (size_t x = 0; x < (size_t)types->end; x++) {
			if (strcmp(types->item[x], "\"damage\"") == 0) {
				damage = true;
			} else if (strcmp(types->item[x], "\"custom_model_data\"") == 0) {
				custom_model_data = true;
			}
		}

		for (size_t x = 0; x < overrides->count; x++) {
			value = placeholder;

			float index = 0;
			for (size_t y = 0; y < overrides->value[x].count; y++) {
				if (strcmp(overrides->value[x].value[y].declaration, "\"model\"") == 0) {
					model = overrides->value[x].value[y].value;
				} else if (strcmp(overrides->value[x].value[y].declaration, "\"predicate\"") == 0) {
					query = overrides->value[x].value[y].value;
				}
			}

			for (size_t y = 0; custom_model_data && y < (query->count + 1); y++) {
				if (value->value == NULL || strcmp(value->value->value[1].value->declaration, "\"minecraft:custom_model_data\"") != 0) {
					sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:custom_model_data\",\n\t\"entries\": [\n\t],\n\t\"fallback\": {\n\t\t\"type\": \"minecraft:model\",\n\t\t\"model\": \"minecraft:item/%s\"\n\t}\n}", file[0]->name);
					temp = processOBJ(buffer);

					if (value->value != NULL) {
						freeOBJ(&value->value[0]);
						value->count--;
						value->value = NULL;
					}

					addOBJ(&value, temp);
					temp = NULL;
				}

				if (y < query->count && strcmp(query->value[y].declaration, "\"custom_model_data\"") == 0) {
					value = &placeholder->value->value[2];
					index = strtof(query->value[y].value->declaration, NULL);

					for (size_t z = 0; z < (value->value->count + 1); z++) {
						if (z < value->value->count && index == strtof(value->value->value[z].value[1].value->declaration, NULL)) {
							value = &value->value->value[z].value[0];
							break;
						} else if (z >= value->value->count) {
							sprintf(buffer, "{\n\t\"model\": {},\n\t\"threshold\": %d\n}", (int)index);
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

					break;
				} else if (y >= query->count) {
					value = &value->value->value[3]; //Pointing to fallback when the predicate doesn't have cmd predicate
					break;
				}
			}

			for (size_t y = 0; damage && y < (query->count + 1); y++) {
				if (value->value == NULL || strcmp(value->value->value[1].value->declaration, "\"minecraft:damage\"") != 0) {
					sprintf(buffer, "{\n\t\"type\": \"minecraft:range_dispatch\",\n\t\"property\": \"minecraft:damage\",\n\t\"normalize\": false\n\t\"entries\": [\n\t],\n\t\"fallback\": {\n\t\t\"type\": \"minecraft:model\",\n\t\t\"model\": \"minecraft:item/%s\"\n\t}\n}", file[0]->name);
					temp = processOBJ(buffer);

					if (value->value != NULL) {
						freeOBJ(&value->value[0]);
						value->count--;
						value->value = NULL;
					}
					
					addOBJ(&value, temp);
					temp = NULL;
				}

				if (y < query->count && strcmp(query->value[y].declaration, "\"damage\"") == 0) {
					value = &value->value->value[3]; //Pointing to entries
					index = strtof(query->value[y].value->declaration, NULL);

					for (size_t z = 0; z < (value->value->count + 1); z++) {
						if (z < value->value->count && index == strtof(value->value->value[z].value[1].value->declaration, NULL)) {
							value = &value->value->value[z].value[0];
							break;
						} else if (z >= value->value->count) {
							sprintf(buffer, "{\n\t\"model\": {},\n\t\"threshold\": %d\n}", (int)index);
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

					break;
				} else if (y >= query->count) {
					value = &value->value->value[4];
				}
			}

			pointer = strrchr(model->declaration, '/');
			sscanf(pointer + 1, "%255[^\"]", name);
			sprintf(buffer, "{\n\t\"type\": \"minecraft:model\",\n\t\"model\": \"minecraft:item/%s\"\n}", name);

			temp = processOBJ(buffer);
			if (value->value != NULL) {
				freeOBJ(&value->value[0]);
				value->count--;
				value->value = NULL;
			}

			addOBJ(&value, temp);
			temp = NULL;
		}

		placeholder = placeholder->parent;
		break;
	default:
		break;
	}

	overrides = overrides->parent->parent;
	for (size_t x = 0; x < overrides->count; x++) {
		if (strcmp(overrides->value[x].declaration, "\"overrides\"") == 0) {
			delOBJ(&overrides, x);
			break;
		}
	}

	free(file[0]->tab);
	file[0]->tab = printJSON(overrides);
	indentJSON(&file[0]->tab);
	freeOBJ(overrides);

	ARCHIVE* item = (ARCHIVE*)malloc(sizeof(ARCHIVE));
	item->name = strdup(file[0]->name);
	item->tab = printJSON(placeholder);
	indentJSON(&item->tab);
	item->size = strlen(item->tab);

	addFile(&location, item);
	item = NULL;
	*/
} 

void executeInstruct(FOLDER* target, FOLDER* assets, char* instruct) {
	FOLDER *navigator, *cursor, *templates;
	char *pointer, *checkpoint, *save, buffer[1024], command[512], namespace[256], name[256], *path = calloc(256, sizeof(char));
	int input, pin = report_end, line = 0, command_line = 0;
	QUEUE *arguments;

	struct FILEPTR {
		FOLDER *container;
		int index;
	} file;

	file.container = NULL;
	file.index = -1;

	getcwd(path, 256);
	returnString(&path, "templates");

	templates = getFolder(path, -1);

	nodelay(window, true);
	
	for (pointer = instruct; pointer != NULL; pointer++, pointer = strchrs(pointer, 2, '>', '<')) {
		input = wgetch(window);
		wclear(miniwin);

		if (input == KEY_RESIZE) {
			refreshWindows();
		}

		input = (report_end - pin) > (size_t)(getmaxy(miniwin) - 2) ? (int)(report_end - (getmaxy(miniwin) - 2)) : pin;
		mvwprintLines(miniwin, report, 1, 1, input, report_end);

		box(miniwin, 0, 0);
		wrefresh(miniwin);

		input = (pointer[0] == '>') ? 0 : 1;
		line++;

		if (sscanf(pointer + 3, "%255[^\"]", namespace) == 0) {
			logger("Error! Failed to get file path in line %d! Skipping to next iteraction\n", line);
			continue;
		}
		pointer += strlen(namespace) + 4;
		pointer = strnotchr(pointer, 3, ' ', '\n', '\t');

		// Cropping file instruction
		checkpoint = strchrs(pointer, 2, '>', '<');
		snprintf(buffer, 1023, "%.*s", (int)(checkpoint - pointer), pointer);

		switch (input) {
		case 0:
			navigator = localizeFolder(target, namespace, false);
			if (navigator == NULL) {
				logger("Failed to locate %s in target folder!\n", namespace);
				continue;
			}
			break;
		case 1:
			navigator = localizeFolder(target, namespace, false);
			if (navigator == NULL) {
				logger("Failed to locate %s in assets folder!\n", namespace);
				continue;
			}
			break;
		}

		// Linking folder and file
		file.container = navigator;
		if ((save = strrchr(namespace, '/')) != NULL) {
			save++;
		}
		sscanf(save, "%255[^\"]", name);

		file.index = -1;
		for (int x = 0; strlen(name) > 0 && x < (int)file.container->count; x++) {
			if (strcmp(file.container->content[x].name, name) == 0) {
				file.index = x;
				break;
			}
		}

		if (namespace[strlen(namespace) - 1] != '/' && file.index == -1) {
			logger("Warning! FILE <%s> wasn't found!\n", namespace);
			continue;
		}

		command_line = 0;
		for (
			checkpoint = &buffer[0];
			checkpoint != NULL;
			checkpoint = strchr(checkpoint, ';'), checkpoint = strnotchr(checkpoint, 4, ';', ' ', '\n', '\t')
		) {
			command_line++;
			arguments = initQueue(8);

			if (sscanf(checkpoint, "%511[^;]", command) == 0) {
				logger("Error! Failure when copying command %d in iteraction %d\n", command_line, line);
			}

			// Processing line arguments into queue
			for (save = &command[0]; save != NULL; save = strchr(save + 1, ' ')) {
				if (save[1] == '{') {
					char* backup;
					for (int x = 2, y = 1; save[x] != '\0'; x++) {
						if (save[x] == '{') {
							y++;
						} else if (save[x] == '}') {
							y--;
						}

						if (y == 0) {
							backup = &save[x + 1];
							break;
						}
					}
					
					snprintf(name, (int)(backup - save), save + 1);
					save += strlen(name);
				} else if (sscanf(save, "%255s", name) == 0) {
					logger("Warning! Failed to get argument in iteraction %d, line %d\n", line, command_line);
					continue;
				}

				enQueue(arguments, name);
			}

			if (strcmp(arguments->item[0], "move") == 0 || strcmp(arguments->item[0], "copy") == 0) {
				if (sscanf(arguments->item[1] + 1, "%255[^\"]", namespace) == 0) {
					logger("Failed to process file path string!\n");
					break;
				}

				if ((save = strrchr(namespace, '/')) != NULL) {
					sprintf(name, "%s", save + 1);
				} else {
					name[0] = '\0';
				}

				cursor = localizeFolder(target, namespace, true);
				if (cursor == NULL) {
					logger("Error! Failed to find <%s>!\n", namespace);
					break;
				}

				if (file.index == -1) { // Target is a folder
					FOLDER* mirror = dupFolder(navigator);

					if (strcmp(arguments->item[0], "move") == 0) {
						FOLDER* temp = navigator->parent;
						for (int x = 0; x < (int)(temp->subcount + 1); x++) {
							if (x < (int)temp->subcount && strcmp(temp->subdir[x].name, navigator->name) == 0) {
								delFolder(&temp, x);
								break;
							} else if (x == (int)temp->subcount) {
								logger("Warning! Failed to find origin <%s> folder to delete!\n", navigator->name);
							}
						}

						logger("Moving <%s> to <%s>\n", cursor->name, namespace);
					} else {
						logger("Copying <%s> to <%s>\n", cursor->name, namespace);
					}

					if (strcmp(mirror->name, cursor->name) == 0) {
						logger("A match to <%s> was found at <%s>, merging contents\n", mirror->name, namespace);
						overrideFiles(cursor, mirror);
						freeFolder(mirror);
					} else {
						addFolder(&cursor, mirror);
					}

					mirror = NULL;
				} else { // Target is a file
					ARCHIVE* mirror = dupFile(&file.container->content[file.index]);
					if (strlen(name) > 0) {
						free(mirror->name);
						mirror->name = strdup(name);
					}

					addFile(&cursor, mirror);

					if (strcmp(arguments->item[0], "move") == 0) {
						logger("Moving <%s> to %s\n", file.container->content[file.index].name, cursor->name);
						delFile(&navigator, file.index);
					} else {
						logger("Copying <%s> to %s\n", mirror->name, cursor->name);
					}
					mirror = NULL;
				}
			} else if (strcmp(arguments->item[0], "remove") == 0) {
				if (file.index == -1) {
					cursor = navigator->parent;

					for (int x = 0; x < (int)cursor->subcount; x++) {
						if (strcmp(cursor->subdir[x].name, navigator->name) == 0) {
							logger("FOLDER removing <%s>\n", cursor->subdir[x].name);
							delFolder(&cursor, x);
							navigator = NULL;
							break;
						}
					}
				} else {
					navigator = file.container;
					for (int x = 0; x < (int)navigator->count; x++) {
						if (strcmp(navigator->content[x].name, file.container->content[file.index].name) == 0) {
							logger("FILE removing <%s>\n", navigator->content[x].name);
							delFile(&navigator, x);
							break;
						}
					}
				}
			} else if (strcmp(arguments->item[0], "edit") == 0) {
				if (strcmp(arguments->item[1], "name") == 0) {
					if (sscanf(arguments->item[2] + 1, "%255[^\"]", name) == 0) {
						logger("Error! Couldn't process filename <%s>\n", arguments->item[2]);
						break;
					}
					if (file.index == -1) {
						logger("FOLDER <%s> updated to <%s>", navigator->name, name);

						free(navigator->name);
						navigator->name = strdup(name);
					} else {
						logger("FILE <%s> updated to <%s>", file.container->content[file.index].name, name);

						free(file.container->content[file.index].name);
						file.container->content[file.index].name = strdup(name);
					}
				} else if (strcmp(arguments->item[1], "display") == 0 || strcmp(arguments->item[1], "texture_path") == 0) {
					OBJECT *value, *placeholder, *query;
					sprintf(name, "%s", (strcmp(arguments->item[1], "display") == 0) ? "\"display\"" : "\"textures\"");

					logger("FILE <%s> %s updated \n", file.container->content[file.index].name, name);

					placeholder = processJSON(file.container->content[file.index].tab);
					char* debug = printJSON(placeholder);
					/* 
					for (size_t x = 0; x < placeholder->count; x++) {
						if (strcmp(placeholder->value[x].declaration, name) == 0) {
							query = placeholder->value[x].value;
							break;
						}
					}

					if (query == NULL) {
						logger("Warning! Failed to find %s member in file model!", name);
						break;
					}
					
					// Executing display cleanup
					if (strcmp(arguments->item[2], "set") == 0) {
						while(query->count > 0) {
							delOBJ(&query, 0);
						}

						deQueue(arguments, 2);
					}
					value = processOBJ(arguments->item[2]);

					// Executing new members placement
					for (size_t x = 0; x < value->count; x++) {
						OBJECT* mirror = NULL;
						mirror = dupOBJ(&value->value[x]);

						for (size_t y = 0; y < (query->count + 1); y++) {
							if (y < query->count && strcmp(mirror->declaration, query->value[y].declaration) == 0) {
								freeOBJ(&query->value[y]);
								
								mirror->parent = query;
								query->value[y] = *mirror;
								break;
							} else if (y == query->count) {
								addOBJ(&query, mirror);
								break;
							}
						}
					}
					char *model = printJSON(placeholder);
					indentJSON(&model);

					free(file.container->content[file.index].tab);
					file.container->content[file.index].tab = model;
					file.container->content[file.index].size = strlen(model);
					
					model = NULL;

					freeOBJ(value);
					freeOBJ(placeholder);
					*/
				} else if (strcmp(arguments->item[1], "dimentions") == 0) {
					int width, height;
					if (sscanf(arguments->item[2], "%dx%d", &width, &height) == 0) {
						logger("Error! Failed to process new file dimentions \"%s\"", arguments->item[2]);
						break;
					}

					ARCHIVE* temp = &file.container->content[file.index];
					resizePNGFile(&temp, width, height);
					temp = NULL;
				}
			} else if (strcmp(arguments->item[0], "autofill") == 0) {
				/* 
				logger("FILE <%s> Executing atlas autofill\n", file.container->content[file.index].name);
				QUEUE* folders;
				int dirnumber = 0, position[16];
				char *temp;
                size_t length;
				OBJECT *placeholder, *query, *value;
				placeholder = processOBJ(file.container->content[file.index].tab);

				for (int x = 0; x < 16; x++) {
                    position[x] = 0;
                }

				while(placeholder->value->value->count > 0) {
					delOBJ(&placeholder->value->value, 0);
				}
				
                navigator = localizeFolder(target, "minecraft:textures/", false);
                if (navigator == NULL) {
                    logger("Error! no textures folder were found in the minecraft folder!\n");
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

                free(file.container->content[file.index].tab);
                file.container->content[file.index].tab = printJSON(placeholder);
				indentJSON(&file.container->content[file.index].tab);
                file.container->content[file.index].size = strlen(file.container->content[file.index].tab);
				*/
			} else if (strcmp(arguments->item[0], "disassemble") == 0) {
				/* 
				OBJECT *mirror, *copy, *elements, *cube, *base = NULL, *children, *placeholder, *query, *value;
				QUEUE *groups;
				FOLDER* location;
				bool trim = false;

				placeholder = processOBJ(file.container->content[file.index].tab);

				if (strcmp(arguments->item[2], "trim") == 0) {
					trim = true;
					deQueue(arguments, 2);
				}
				if (sscanf(arguments->item[2] + 1, "%255[^\"]", namespace) == 0) {
					logger("Error! Failed to process disassemble file path at iteraction %d\n", line);
					break;
				}

				location = localizeFolder(target, namespace, true);

				//Cleaning the reference
				mirror = dupOBJ(placeholder);
				for (size_t x = 0; x < mirror->count; x++) {
					if (strcmp(mirror->value[x].declaration, "\"elements\"") == 0) {
						query = mirror->value[x].value;
						while (query->count > 0) {
							delOBJ(&query, 0);
						}

					} else if (strcmp(mirror->value[x].declaration, "\"groups\"") == 0) {
						query = dupOBJ(&mirror->value[x]);
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
									if (sscanf(base->value[x].value->value[y].declaration, "%d", &index) == 0) {
										logger("Failed to extract children's index! skipping this cube\n");
										continue;
									} 
									
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
								if (sscanf(children->value[z].declaration, "%d", &index) == 0) {
									logger("Failed to extract children's index! skipping this cube\n");
									continue;
								} 
								cube = dupOBJ(&elements->value[index]);

								addOBJ(&copy, cube);
								cube = NULL;
							}
							break;
						}
					}

					if (sscanf(groups->item[x], "\"%[^\"]s\"", file_name) == 0) {
						logger("Error! Failed to get filename!\n");
						return;
					}
					
					permutate = malloc(sizeof(ARCHIVE));
					permutate->name = strdup(file_name);
					permutate->tab = printJSON(value);
					permutate->size = strlen(permutate->tab);

					indentJSON(&permutate->tab);

					logger("FILE <%s> placed at %s\n", permutate->name, namespace);

					addFile(&location, permutate);
					permutate = NULL;

					freeOBJ(value);
				}
				freeOBJ(query);

				navigator = file.container;
				for (size_t x = 0; x < navigator->count && trim == true; x++) {
					if (strcmp(navigator->content[x].name, file.container->content[file.index].name) == 0) {
						delFile(&navigator, x);
						break;
					}
				}

				free(file.container->content[file.index].tab);
				file.container->content[file.index].tab = printJSON(placeholder);
				indentJSON(&file.container->content[file.index].tab);
				 */
			} else if (strcmp(arguments->item[0], "paint") == 0) {
				logger("Executing texture paint on %s\n", file.container->content[file.index].name);
				int width, height;
				int lines, collums;
				int bit_depth, color_type;
				int bpp[2];
				png_bytep *texture, *pixels, *pallet;
				FOLDER* location;

				texture = pixels = pallet = NULL;

				if (getPNGPixels(&file.container->content[file.index], &texture, &width, &height, &color_type, &bit_depth, NULL, true) == 0) {
					logger("Failed to read png file!\n");
					checkpoint = NULL;
					continue;
				}

				if (texture == NULL) {
					continue;
				}
				
				bpp[0] = (color_type == PNG_COLOR_TYPE_RGBA) ? 4 : 3;
				// Map
				if (sscanf(arguments->item[1] + 1, "%255[^\"]", namespace) == 0) {
					logger("Error! Failed to process texture color map's path at iteraction %d!\n", line);
					break;
				}

				if (strstr(namespace, "./") != NULL) {
					location = localizeFolder(target, namespace, false);

					if (location == NULL) {
						logger("Error! Couldn't find %s at target folder\n", namespace);
						checkpoint = NULL;
						continue;
					}
				} else {
					location = localizeFolder(assets, namespace, false);
					if (location == NULL) {
						logger("Error! Couldn't find %s at assets folder", namespace);
						checkpoint = NULL;
						continue;
					}
				}

				if ((save = strrchr(namespace, '/')) == NULL) {
					sprintf(name, "%s", namespace);
				} else {
					sprintf(name, "%s", save + 1);
				}

				for (size_t x = 0; x < (location->count + 1); x++) {
					if (x < location->count && strcmp(location->content[x].name, name) == 0) {
						getPNGPixels(&location->content[x], &pixels, &collums, &lines, &color_type, &bit_depth, NULL, true);
						break;
					} else if (x == location->count) {
						logger("Warning! Failed to find <%s>!\n", name);
					}
				}
				if (pixels == NULL) {
					free(texture);
					continue;
				}
				bpp[1] = (color_type == PNG_COLOR_TYPE_RGBA) ? 4 : 3;

				// Pallet
				if (sscanf(arguments->item[2] + 1, "%255[^\"]", namespace) == 0) {
					logger("Error! Failed to process texture color map's path at iteraction %d!\n", line);
					break;
				}

				if (strstr(namespace, "./") != NULL) {
					location = localizeFolder(target, namespace, false);

					if (location == NULL) {
						logger("Error! Couldn't find %s at target folder", namespace);
						checkpoint = NULL;
						continue;
					}
				} else {
					location = localizeFolder(assets, namespace, false);
					if (location == NULL) {
						logger("Error! Couldn't find %s at assets folder", namespace);
						checkpoint = NULL;
						continue;
					}
				}

				if ((save = strrchr(namespace, '/')) == NULL) {
					sprintf(name, "%s", namespace);
				} else {
					sprintf(name, "%s", save + 1);
				}

				for (size_t x = 0; x < (location->count + 1); x++) {
					if (x < location->count && strcmp(location->content[x].name, name) == 0) {
						getPNGPixels(&location->content[x], &pallet, NULL, NULL, NULL, NULL, NULL, true);
						break;
					} else if (x == location->count) {
						logger("Warning! Failed to find <%s>!\n", name);
					}
				}
				if (pallet == NULL) {
					free(texture);
					free(pixels);
					continue;
				}

				for (int x = 0; x < height; x++) {
					for (int y = 0; y < width; y++) {
						png_bytep compare = &texture[x][y * bpp[0]];

						for (int z = 0; z < collums; z++) {
							png_bytep px = &pixels[0][z * bpp[1]];;

							if (bpp[0] == 4 && compare[3] == 0) {
								continue;
							}

							if (
								px[0] == compare[0]
								&& px[1] == compare[1]
								&& px[2] == compare[2]
							) {
								texture[x][(y * bpp[0]) + 0] = pallet[0][(z * bpp[1]) + 0];
								texture[x][(y * bpp[0]) + 1] = pallet[0][(z * bpp[1]) + 1];
								texture[x][(y * bpp[0]) + 2] = pallet[0][(z * bpp[1]) + 2];

								break;
							}
						}
					}
				}
				
				printPNGPixels(&file.container->content[file.index], texture, width, height);
				logger("FILE %s painted\n", file.container->content[file.index].name);

				free(pallet);
				free(texture);
				free(pixels);
			} else if (strcmp(arguments->item[0], "permutate_texture") == 0) {
				/* 
				int width, height, texture_count = 0, bit_deph, color_type[2], bpp[2];
				int lines, collums;
				ARCHIVE *map, *copies[32];
				FOLDER* location;
				OBJECT* list;
				png_bytep *texture, *pixels, *pallet[32], *to_paint[32];
				
				//Getting texture map name
				if (sscanf(arguments->item[1] + 1, "%255[^\"]", namespace) == 0) {
					logger("Error! Failed to process texture color map's path at iteraction %d!\n", line);
					break;
				}
				
				for (size_t x = 0; x < navigator->count; x++) {
					if (strcmp(navigator->content[x].name, name) == 0) {
						map = &navigator->content[x];
						break;
					}
				}

				//Getting pallets list
				list = processOBJ(arguments->item[2]);

				for (size_t x = 0; x < list->count; x++) {
					char* name_pointer;
					if (sscanf(list->value[x].declaration, "\"%[^\"]\"", namespace) == 0) {
						logger("Error! Failed to extract file pallet %d name!\n", x);
						continue;
					}

					if (strstr(name, "./") != NULL) {
						location = localizeFolder(target, namespace, false);
	
						if (location == NULL) {
							logger("Error! Couldn't find %s at target folder", namespace);
							continue;
						}
					} else {
						location = localizeFolder(assets, namespace, false);
						if (location == NULL) {
							logger("Error! Couldn't find %s at assets folder", namespace);
							continue;
						}
					}

					if ((name_pointer = strrchr(namespace, '/')) == NULL) {
						sprintf(name, "%.*s", 255, namespace);
						name_pointer = name;
					} else {
						sprintf(name, "%s", name_pointer + 1);
					}

					for (size_t y = 0; y < navigator->count; y++) {

						if (strcmp(location->content[y].name, namespace) == 0) {
							copies[texture_count] = dupFile(&file.container->content[file.index]);

							name_pointer = strchr(name, '.');
							sprintf(namespace, "%.*s_%s", (int)(name_pointer - name), name, copies[texture_count]->name);
							free(copies[texture_count]->name);
							copies[texture_count]->name = strdup(namespace);

							ARCHIVE *temp = &location->content[y];
							ARCHIVE *mirror = copies[texture_count];

							getPNGPixels(temp, &pallet[texture_count], NULL, NULL, NULL, NULL, NULL, true);
							getPNGPixels(mirror, &to_paint[texture_count], NULL, NULL, NULL, NULL, NULL, true);

							if (pallet[texture_count] == NULL || to_paint[texture_count] == NULL) {
								logger("Error! Failed to process png files: %s, %s\n", location->content[y].name, copies[texture_count]->name);
								continue;
							} else {
								texture_count++;
							}

							temp = mirror = NULL;
							break;
						}
					}
				}
				
				//Continue with color substitution
				getPNGPixels(map, &pixels, &collums, &lines, &bit_deph, &color_type[0], NULL, true);
				getPNGPixels(&file.container->content[file.index], &texture, &width, &height, &bit_deph, &color_type[1], NULL, true);

				if (pixels == NULL || texture == NULL) {
					logger("Error! Failed to process png files: %s, %s\n", map->name, file.container->content[file.index].name);
					break;
				}

				bpp[1] = (color_type[0] == PNG_COLOR_TYPE_RGBA) ? 4 : 3;
				bpp[0] = (color_type[1] == PNG_COLOR_TYPE_RGBA) ? 4 : 3;

				for (int x = 0; x < height; x++) {

					for (int y = 0; y < width; y++) {
						png_bytep compare = &texture[x][y * bpp[0]];

						for (int z = 0; z < collums; z++) {
							png_bytep px = &pixels[0][z * bpp[1]];

							if (bpp[0] == 4 && compare[3] == 0) {
								continue;
							}

							if (
								px[0] == compare[0]
								&& px[1] == compare[1]
								&& px[2] == compare[2]
							) {
								for (int a = 0; a < texture_count; a++) {
									to_paint[a][x][(y * bpp[0]) + 0] = pallet[a][0][(z * bpp[1]) + 0];
									to_paint[a][x][(y * bpp[0]) + 1] = pallet[a][0][(z * bpp[1]) + 1];
									to_paint[a][x][(y * bpp[0]) + 2] = pallet[a][0][(z * bpp[1]) + 2];
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
					ARCHIVE *mirror = copies[x];
					printPNGPixels(mirror, to_paint[x], width, height);
					addFile(&navigator, copies[x]);
					logger("FILE <%s> was added to the same location as the base file\n", mirror->name, navigator->name);

					free(to_paint[x]);
					free(pallet[x]);

					mirror = NULL;
				}
				*/
			} else if (strcmp(arguments->item[0], "convert_overrides") == 0) {
				logger("FILE <%s> translating overrides\n", file.container->content[file.index].name);
				ARCHIVE* temp = &file.container->content[file.index];
				overridesFormatConvert(navigator, &temp);
				temp = NULL;
			}

			endQueue(arguments);
		}
	}
	nodelay(window, false);
}

void printZip(FOLDER* folder, char** path) {
	int dirNumber, dirPosition[FOLDERCHUNK], input;
	size_t pin = report_end;
	char location[1024], *pointer;
	bool done = false;
	FOLDER* navigator = folder;
	sprintf(location, "%s\\%s.zip", path[0], folder->name);
	zipFile rp = zipOpen(location, APPEND_STATUS_CREATE);
	if (rp == NULL) {
		logger("Error creating zip file!\n");
		return;
	}

	for (int x = 0; x < FOLDERCHUNK; x++) {
		dirPosition[x] = 0;
	}
	location[0] = '\0';

	nodelay(window, true);

	while (!done) {
		input = wgetch(window);
		wclear(miniwin);

		if (input == KEY_RESIZE) {
			refreshWindows();
		}

		input = (report_end - pin) > (size_t)(getmaxy(miniwin) - 2) ? (report_end - (getmaxy(miniwin) - 2)) : pin;
		mvwprintLines(miniwin, report, 1, 1, input, report_end);

		box(miniwin, 0, 0);
		wrefresh(miniwin);

		while (navigator->subcount > 0) {
			navigator = &navigator->subdir[dirPosition[dirNumber]];
			snprintf(location + strlen(location), 1024 - strlen(location), "%s/", navigator->name);
			
			logger("%s: FOLDER <%s>\n", folder->name, location);

			zipOpenNewFileInZip(rp, location, NULL, NULL, 0, NULL, 0, NULL, 0, 0);
			zipCloseFileInZip(rp);

			dirPosition[dirNumber]++;
			dirNumber = (dirNumber < 16) ? (dirNumber + 1) : dirNumber;
		}
		pointer = &location[strlen(location)];

		for (int x = 0; x < (int)navigator->count; x++) {
			snprintf(pointer, 1024 - strlen(location), "%s", navigator->content[x].name);
			logger("%s FILE: <%s>\n", folder->name, location);

			if (zipOpenNewFileInZip(rp, location, NULL, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK) {
				logger("Failed to create %s!\n");
				zipClose(rp, NULL);
				return;
			}

			zipWriteInFileInZip(rp, navigator->content[x].tab, navigator->content[x].size);
			zipCloseFileInZip(rp);
		}

		pointer[0] = '\0';

		while (dirPosition[dirNumber] == (int)navigator->subcount && dirNumber > -1) {
			if ((pointer = strrchr(location, '/')) != NULL) {
				pointer[0] = '\0';
			}
			if ((pointer = strrchr(location, '/')) != NULL) {
				pointer[1] = '\0';
			} else {
				location[0] = '\0';
			}

			dirPosition[dirNumber] = 0;
			dirNumber--;
			navigator = navigator->parent;
		}

		done = dirNumber < 0 ? true : false;
	}
	nodelay(window, false);

	zipClose(rp, NULL);
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

	//Starting the menu;
    initscr();
    setlocale(LC_ALL|~LC_NUMERIC, "");
    int input = 0, cursor[3], optLenght, actionLenght[2], diretrix[2];
    cursor[0] = cursor[1] = cursor[2] = n_entries = diretrix[0] = diretrix[1] = 0;
    bool quit = false, update = true;
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
	nodelay(window, false);

	logger("Getting lang text\n");
    returnString(&path, "lang");
    lang = getFolder(path, -1);
    translated = getLang(lang, 0);

	logger("Getting instructions from folder\n");
	returnString(&path, "path");
    returnString(&path, "instructions");
	FOLDER* instructions = getFolder(path, -1);

	logger("Linking resourcepacks folder\n");
    returnString(&path, "path");
    returnString(&path, "resourcepacks");
    FOLDER* targets = createFolder(NULL, "targets");

	// Creating missing non essential folders
	if (targets == NULL) {
		logger("Warning! resourcepacks folders was missing on initialization, recreating the folder.\n");
		mkdir(path);
		targets = createFolder(NULL, "targets");
	}

    refreshWindows();
    optLenght = mvwprintLines(NULL, translated[0], 0, 1, 0, -1);

    query = initQueue(8);
    entries = initQueue(8);

	display = (char*)calloc((_miniwin->size_x * _miniwin->size_y) + 1, sizeof(char));
	if (display == NULL) {
		logger("Failed to allocate memory for minwin buffer! %s\n", strerror(errno));
		return 1;
	}

    logger("Starting screen\n");

    while (!quit) {
        //Draw miniwindow
		if (update == true || entries->end == 0) {
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
                } else {
                    mvwprintLines(miniwin, translated[1], 1, 1, 1, 1);
                }
                n_entries = entries->end;

                break;
            case 1:
                if (targets->subcount < 1) {
                    mvwprintLines(miniwin, translated[1], 1, 1, 3, 3);
                    n_entries = 0;
                } else if (diretrix[0] == 0) { // Printing tool options
					for (int x = 0; x < 3; x++) {
						optLenght = mvwprintLines(miniwin, translated[1], (1 + x), 1, (10 + x), (10 + x));
					}
					n_entries = 3;
                } else if (diretrix[0] != 0 && diretrix[1] == 1) { // Printing scanned resourcepacks for selection
					mvwprintLines(miniwin, translated[1], 0, 1, 17, 17);

					for (int x = 0; x < (int)targets->subcount; x++) {
						mvwprintw(miniwin, 1 + x, 1, "%s %s", targets->subdir[x].name, diretrix[0] == 2 ? "[ ]": "");

						optLenght = (int)strlen(targets->subdir[x].name) > optLenght ? (int)strlen(targets->subdir[x].name) : optLenght; 
					}

					for (int x = 0; diretrix[0] == 1 && x < query->end; x++) {
						mvwprintw(miniwin, 1 + query->value[x], strlen(query->item[x]) + 2, "%s", x == 0 ? "TARGET" : "ASSETS");
					}

					for (int x = 0; diretrix[0] == 2 && x < query->end; x++) {
						mvwprintw(miniwin, 1 + query->value[x], strlen(query->item[x]) + 2, "[X]");
					}
					
					mvwprintLines(miniwin, translated[1], targets->subcount + 1, 1, 15, 16);
					n_entries = targets->subcount + 2;
					
				} else if (diretrix[0] == 1 && diretrix[1] == 2) { // Printing instructions file for selection
					mvwprintLines(miniwin, translated[1], 0, 1, 18, 18);

					for (int x = 0; x < (int)instructions->count; x++) {
						mvwprintw(miniwin, 1 + x, 1, "%s", instructions->content[x].name);
						
						optLenght = (int)strlen(instructions->content[x].name) > optLenght ? (int)strlen(instructions->content[x].name) : optLenght;
					}

					mvwprintLines(miniwin, translated[1], 1 + instructions->count, 1, 16, 16);
					n_entries = instructions->count + 1;
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
				if (diretrix[1] == 1) {
					if (cursor[0] < (int)targets->subcount) {
						optLenght = strlen(targets->subdir[cursor[0]].name) + 1;
					} else {
						optLenght = (int)mvwprintLines(NULL, translated[1], targets->subcount + 1, 1, (15 + (cursor[0] - targets->subcount)), (15 + (cursor[0] - targets->subcount)));
					}
				} if (diretrix[1] == 2) {
					if (cursor[0] < (int)instructions->count) {
						optLenght = strlen(instructions->content[cursor[0]].name);
					} else {
						optLenght = (int)mvwprintLines(NULL, translated[1], targets->subcount + 1, 1, (15 + (cursor[0] - instructions->count)), (15 + (cursor[0] - instructions->count)));
					}
				}
                mvwchgat(miniwin, cursor[0]+1, 1, optLenght, A_STANDOUT, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name) + 1, A_STANDOUT, 0, NULL);
                break;
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
				if (diretrix[1] == 1) {
					if (cursor[0] < (int)targets->subcount) {
						optLenght = strlen(targets->subdir[cursor[0]].name) + 1;
					} else {
						optLenght = (int)mvwprintLines(NULL, translated[1], targets->subcount + 1, 1, (15 + (cursor[0] - targets->subcount)), (15 + (cursor[0] - targets->subcount)));
					}
				} if (diretrix[1] == 2) {
					if (cursor[0] < (int)instructions->count) {
						optLenght = strlen(instructions->content[cursor[0]].name);
					} else {
						optLenght = (int)mvwprintLines(NULL, translated[1], targets->subcount + 1, 1, 16, 16);
					}
				}
				mvwchgat(miniwin, cursor[0]+1, 1, optLenght, A_NORMAL, 0, NULL);
                break;
            case 2:
                mvwchgat(miniwin, cursor[0]+1, 1, strlen(lang->content[cursor[0]].name) + 1, A_NORMAL, 0, NULL);
                break;
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
        case ENTER:
            if (cursor[2] == 0) { // Focus on the sidebar
                switch (cursor[1])
                {
                case 0: // scanning folder from query
					if (query->end > 0 && confirmationDialog(translated[1], 5, actionLenght, 0)) {
						FOLDER* temp;
						for (int x = 0; x < query->end; x++) {
							temp = getFolder(path, query->value[x]);
							
							if (temp != NULL) {
								addFolder(&targets, temp);
								entries->value[query->value[x]] = 2;
								temp = NULL;
							} else {
								confirmationDialog(translated[1], 21, actionLenght, 1);
								update = true;
							}
						}
						
						confirmationDialog(translated[1], 6, actionLenght, 1);
						endQueue(query);
						query = initQueue(8);
					}
					update = true;

                    break;
				case 1:
                    break;
                case 3:
                    quit = true;
                    break;
                }
            } else if (cursor[2] == 1) { // Focus on miniwin

                switch (cursor[1])
                {
                case 0: // Mounting folder query
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

                    for (int x = 0; x < entries->end; x++) {
                        mvwprintw(miniwin, 1+x, 1, "%s [%c]  ", entries->item[x], (entries->value[x] == 1) ? 'X' : (entries->value[x] == 2) ? '#' : ' ');
                    }
                        
                    break;
                case 1: // Tools option
					update = true;

					if (diretrix[0] == 0) {
						diretrix[0] = cursor[0];
						diretrix[1]++;

						cursor[0] = 0;
					} else if (diretrix[1] == 1) {
						if (cursor[0] == n_entries - 1) {
							diretrix[1] = diretrix[0] = 0;
							cursor[0] = 0;

							endQueue(query);
							query = initQueue(8);
							break;
						} else if (
							cursor[0] == n_entries - 2
							&& ((diretrix[0] == 0 && query->end < 2) 
								|| (diretrix[0] > 0 && query->end > 0))
						) {
							if (diretrix[0] == 0) { // Merge resourcepacks
								overrideFiles(&targets->subdir[query->value[0]], &targets->subdir[query->value[1]]);
	
								diretrix[1] = diretrix[0] = 0;
								cursor[0] = 0;
								endQueue(query);
								query = initQueue(8);
							} else if (diretrix[0] == 1) {
								diretrix[1]++;
								cursor[0] = 0;
							} else if (diretrix[0] == 2) { // Export resourcepack
								for (int x = 0; x < query->end; x++) {
									printZip(&targets->subdir[query->value[x]], &path);
								}
								confirmationDialog(translated[1], 20, actionLenght, 1);
		
								diretrix[1] = diretrix[0] = 0;
								cursor[0] = 0;
								endQueue(query);
								query = initQueue(8);
							}
							break;
						} else if (cursor[0] < n_entries - 2) {
							for (int x = 0; x < query->end + 1; x++) {
								if (x < query->end && strcmp(targets->subdir[cursor[0]].name, query->item[x]) == 0) {
									deQueue(query, x);
									break;
								} else if (x == query->end) {
									enQueue(query, targets->subdir[cursor[0]].name);
									query->value[query->end-1] = cursor[0];
									break;
								}
							}
						}
					} else if (diretrix[0] == 1 && diretrix[1] == 2) { // Execute instructions
						if (cursor[0] == n_entries - 1) {
							diretrix[1] = diretrix[0] = 0;
							cursor[0] = 0;

							endQueue(query);
							query = initQueue(8);
							break;
						} else {
							FOLDER *second, *recieve;
							second = query->end == 2 ? &targets->subdir[query->value[1]] : NULL;
							recieve = dupFolder(&targets->subdir[query->value[0]]);
							free(recieve->name);
							char name[256], *extension = strrchr(instructions->content[cursor[0]].name, '.');
							extension++;
							snprintf(name, (int)(extension - instructions->content[cursor[0]].name), "%s", instructions->content[cursor[0]].name);
							recieve->name = strdup(name);

							executeInstruct(recieve, second, instructions->content[cursor[0]].tab);
							addFolder(&targets, recieve);
							logger("Executed %s instructions and saved into %s duplicated folder\n", instructions->content[cursor[0]].name, recieve->name);
							confirmationDialog(translated[1], 13, actionLenght, 1);

							diretrix[1] = diretrix[0] = 0;
							cursor[0] = 0;
							
							endQueue(query);
							query = initQueue(8);
						}
					}
					
                    break;
                case 2: // Change language
                    for (int x = 0; x < 3; x++) {
                        free(translated[x]);
                    }
                    free(translated);
                    translated = getLang(lang, cursor[0]);
                    refreshWindows();
                    optLenght = mvwprintLines(NULL, translated[0], 0, 1, 0, -1);

                    update = true;
                    break;
                default:
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
            refreshWindows();
            update = true;
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