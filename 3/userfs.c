#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "userfs.h"
#include <stddef.h>
#include <string.h>
enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
    RETVAL_SUCCESS = 0,
    RETVAL_FAILURE = -1,
    RETVAL_EOF = 0
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char *memory;
	/** How many bytes are occupied. */
	size_t occupied;
	/** Next block in the file. */
	struct block *next;
	/** Previous block in the file. */
	struct block *prev;

	/* PUT HERE OTHER MEMBERS */
};

struct file {
	/** Double-linked list of file blocks. */
	struct block *block_list;
	/**
	 * Last block in the list above for fast access to the end
	 * of file.
	 */
	struct block *last_block;
	/** How many file descriptors are opened on the file. */
	int refs;
	/** File name. */
	char *name;
	/** Files are stored in a double-linked list. */
	struct file *next;
	struct file *prev;

//булева переменная на пометку об скорейшем удалении 
	bool close_delete; 
    size_t file_size;

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;
    struct block *block;        // Pointer to the current block being accessed
    size_t block_position;
    enum open_flags file_flags;
};

static struct filedesc **file_descriptors = NULL;
static int file_descriptor_count = 0;//активные дескрипторы
static int file_descriptor_capacity = 0;//изначальный размер при создании- длина массива, может увеличиться

int allocate_file_descriptor(struct file *file) {
    // Ищем первый доступный дескриптор в массиве file_descriptors.
    for (int i = 0; i < file_descriptor_capacity; i++) {
        if (file_descriptors[i] == NULL) {
            file_descriptors[i] = (struct filedesc *)malloc(sizeof(struct filedesc));

            if (file_descriptors[i] == NULL) {
                // Ошибка выделения памяти для дескриптора.
                return -1;
            }

            file_descriptors[i]->file = file;
            file_descriptors[i]->block = NULL;
            file_descriptors[i]->block_position = 0;
			file_descriptor_count++;
            return i; // Возвращаем индекс выделенного дескриптора.
        }
    }

    // Если не удалось найти доступный дескриптор, то нужно увеличить массив file_descriptors.
    if (file_descriptor_count >= file_descriptor_capacity) {
        // Увеличиваем capacity и перевыделяем память.
		int new_capacity;
		if(file_descriptor_capacity == 0){
			new_capacity = 1;
		}else{
			new_capacity = file_descriptor_capacity * 2;
		}
        
        struct filedesc **new_descriptors = (struct filedesc **)realloc(file_descriptors, new_capacity * sizeof(struct filedesc *));
        if (new_descriptors == NULL) {
            // Ошибка перевыделения памяти.
            return -1;
        } else {
			// Инициализация новой области памяти нулями
			memset(new_descriptors + file_descriptor_capacity, 0, (new_capacity - file_descriptor_capacity) * sizeof(struct filedesc *));
			file_descriptors = new_descriptors;
			file_descriptor_capacity = new_capacity;
		}


        file_descriptors = new_descriptors;
        file_descriptor_capacity = new_capacity;
    }

    // Теперь можно выделить новый дескриптор в первом доступном слоте.
    int new_index = file_descriptor_count;
    file_descriptors[new_index] = (struct filedesc *)malloc(sizeof(struct filedesc));
    if (file_descriptors[new_index] == NULL) {
        // Ошибка выделения памяти для дескриптора.
        return -1;
    }
    file_descriptors[new_index]->file = file;
    file_descriptors[new_index]->block = NULL;
    file_descriptors[new_index]->block_position = 0;
    file_descriptor_count++;

    return new_index; // Возвращаем индекс выделенного дескриптора.
}

//если налл - нет дескриптора, если не нал есть
//если активных дескрипторов больше capacity - errors
enum ufs_error_code
ufs_errno()
{
	return ufs_error_code;
}

int
ufs_open(const char *filename, int flags)
{
	//printf("Filename: %s \n", filename);
	//printf("Flags: %d \n", flags);
    // Проверка аргументов
    if (filename == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

	struct file *file = file_list;
    while (file != NULL) {
    	if (strcmp(file->name, filename) == 0) {
           	// Файл с таким именем уже существует. Увеличим счетчик открытых дескрипторов.
           	file->refs++;
           	ufs_error_code = UFS_ERR_NO_ERR;

            int fd = allocate_file_descriptor(file);
            struct filedesc *file_desc = file_descriptors[fd];
            if (flags == 0){
		        file_desc->file_flags = UFS_READ_WRITE;
            }else{
		        file_desc->file_flags = flags;
            }
           	return fd; // Возвращаем дескриптор существующего файла.
       	}
       	file = file->next;
   	}

    // Проверка флагов
    if (flags & UFS_CREATE) {
    	// Попытка создания файла
		// Создаем новую структуру файла
		struct file *new_file = (struct file *)malloc(sizeof(struct file));
		if (new_file == NULL) {
			// Ошибка выделения памяти
			ufs_error_code = UFS_ERR_NO_MEM;
			return -1;
		}

		// Инициализируем новый файл
		new_file->block_list = NULL;
		new_file->last_block = NULL;
		new_file->refs = 1; // Устанавливаем счетчик открытых дескрипторов в 1
		new_file->name = strdup(filename); // Копируем имя файла
        new_file->file_size = 0;

		if (new_file->name == NULL) {
			// Ошибка выделения памяти для имени файла
			free(new_file);
			ufs_error_code = UFS_ERR_NO_MEM;
			return -1;
		}

		// Добавляем новый файл в список файлов
		new_file->next = file_list;
		new_file->prev = NULL;
		if (file_list != NULL) {
			file_list->prev = new_file;
		}
		file_list = new_file;

		// Возвращаем файловый дескриптор
		int fd = allocate_file_descriptor(new_file);
		if (fd < 0) {
			// Ошибка выделения памяти для дескриптора
			ufs_error_code =  UFS_ERR_NO_MEM;
			return -1;
		}

        struct filedesc *file_desc = file_descriptors[fd];
        if (flags == 0){
		    file_desc->file_flags = UFS_READ_WRITE;
        }else{
		    file_desc->file_flags = flags;
        }

		ufs_error_code = UFS_ERR_NO_ERR;
		return fd;
    	
	}else{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

    
}

ssize_t ufs_write(int fd, const char *buf, size_t size) {
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];
    struct file *file = file_desc->file;

    if (file_desc->file_flags == UFS_READ_ONLY){
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

    const char *current_buffer = buf;
    size_t written_bytes = 0; //size left to be written
    size_t bytes_to_write = 0;
    int count = 0;

    if (file_desc->file->file_size + size> MAX_FILE_SIZE) {
        ufs_error_code = UFS_ERR_NO_MEM;
        return RETVAL_FAILURE;
    }

    if(file_desc->block == NULL){
        file_desc->block = file->last_block;
        //file_desc->block_position = 0;
    }
    
    while (written_bytes < size) {
        if(file_desc->block_position >= BLOCK_SIZE){
            file_desc->block = file_desc->block->next;
            file_desc->block_position = 0;
        }

        if(file_desc->block == NULL){
            struct block *new_block = (struct block *) calloc(sizeof(struct block), 1);
            if (new_block == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                break;
            } 

            new_block->memory = (char *) calloc(sizeof(char), BLOCK_SIZE);
            file_desc->block_position = 0;

            if (new_block->memory == NULL) {
                free(new_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                break;
            }

            new_block->occupied = 0;
            file_desc->block = new_block;
            file_desc->block->prev = file->last_block;
            file_desc->block->next = NULL;

            struct block *tail = file->last_block;

            if(tail != NULL){
                tail->next = file_desc->block;
                file_desc->block->prev = tail;
            }
            
            file->last_block = file_desc->block;
            if(file->block_list == NULL){
                file->block_list = file_desc->block;
            }
        }

        count++;

        bytes_to_write = size - written_bytes;
        if(file_desc->block_position > file_desc->block->occupied){
            file_desc->block_position = file_desc->block->occupied;
        }

        if(bytes_to_write + file_desc->block_position > BLOCK_SIZE){
            bytes_to_write = BLOCK_SIZE - file_desc->block_position;
        }

        char *position_to = file_desc->block->memory + file_desc->block_position;
        memcpy(position_to, current_buffer, bytes_to_write);

        written_bytes += bytes_to_write;
        file_desc->block_position += bytes_to_write;

        if(file_desc->block_position > file_desc->block->occupied){
            file_desc->file->file_size += (file_desc->block_position - file_desc->block->occupied);
            file_desc->block->occupied = file_desc->block_position;
        }

        current_buffer += bytes_to_write;
        
        if(file_desc->file->file_size == MAX_FILE_SIZE){
            break;
        }        
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return size;
}



//сделать проверку на размер файла при желаемом размере записываемого файла 

//сколько в файле, сделать сравнение, считать наименьшее
ssize_t ufs_read(int fd, char *buf, size_t size) {
    // Check if the file descriptor is valid
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];
    struct file *file = file_desc->file;
    char *current_buffer = buf;

    if (file_desc->file_flags == UFS_WRITE_ONLY){
		ufs_error_code = UFS_ERR_NO_PERMISSION;
		return -1;
	}

    size_t bytes_read = 0;
    size_t bytes_to_read = 0;

    if(file_desc->block == NULL){
        file_desc->block = file->block_list;
    }

    if(file_desc->block_position == BLOCK_SIZE){
        file_desc->block = file_desc->block->next;
        file_desc->block_position = 0;
    }

    // Skip already read bytes
    while (bytes_read < size) {
        if(file_desc->block == NULL) break;
        bytes_to_read = size - bytes_read;

        if (bytes_to_read > file_desc->block->occupied - file_desc->block_position){
			bytes_to_read = file_desc->block->occupied - file_desc->block_position;
		}

        char *position_from = file_desc->block->memory + file_desc->block_position;

        memcpy(current_buffer, position_from, bytes_to_read);
        
        current_buffer += bytes_to_read;
        bytes_read += bytes_to_read;

        file_desc->block_position += bytes_to_read;

        if (file_desc->block_position >= file_desc->block->occupied || 
            file_desc->block_position >= BLOCK_SIZE) {
            file_desc->block = file_desc->block->next;
            if(file_desc->block != NULL){
                //file_desc->block = file_desc->block;
                file_desc->block_position = 0;
            }
        }

        /*struct block *next_block = file_desc->block->next;
        if (next_block == NULL) {
            printf("No next block");
            break;
        }
        file_desc->block = next_block;*/
    }

    ufs_error_code = UFS_ERR_NO_ERR;

    return bytes_read;
}

void deleteFile(struct file *file) {
    if (file == NULL) {
        return;
    }

    struct block *current_block = file->block_list;
    while (current_block != NULL) {
    	struct block *next_block = current_block->next;
        if (current_block->memory != NULL) free(current_block->memory);
        free(current_block);
        current_block = next_block;
    }

    // Освобождение памяти, занятой файловой структурой
    free(file->name);
    free(file);

    ufs_error_code = UFS_ERR_NO_ERR;
}

int ufs_close(int fd) {
    // Проверка аргументов
	///printf("File Descriptor: %d \n", fd);
	//printf("File Descriptor Capacity: %d \n", file_descriptor_capacity);

    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];
    struct file *file = file_desc->file;

    // Уменьшение счетчика ссылок на файл
    file->refs--;

    // Освобождение дескриптора
    free(file_desc);
    file_descriptors[fd] = NULL;

	if (file->close_delete && file->refs == 0) {
        deleteFile(file);
    }

	file_descriptor_count--;

    ufs_error_code = UFS_ERR_NO_ERR;
    return 0;
}


int ufs_delete(const char *filename) {
    // Проверка аргументов
    if (filename == NULL || strlen(filename) == 0) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    // Поиск файла с указанным именем
    struct file *file = file_list;
    while (file != NULL) {
        if (strcmp(file->name, filename) == 0) {
            // Файл найден
			// Удаление файла из списка файлов
            if (file->next != NULL) {
    			file->next->prev = file->prev;
			}
			if (file->prev != NULL) {
    			file->prev->next = file->next;
			}else {
    			file_list = file->prev;
			}

            // Проверка наличия открытых дескрипторов
            if (file->refs > 0) {
                // Есть открытые дескрипторы, помечаем файл для удаления
                file->close_delete = true;
                ufs_error_code = UFS_ERR_NO_FILE;
                return 0;
            } else {
                deleteFile(file);
                
            }
        }
        file = file->next;
    }

    // Файл с указанным именем не найден
    ufs_error_code = UFS_ERR_NO_FILE;
    return 0;
}

//сделать рповерку открытых дескрипторов, если нет - полностью освобождаем и удаляем
 //если есть - то из листа удаляем, а файл нет и ждём пока последний декриптор закроется и тогда удаляем 

void ufs_destroy(void) {
    // Закрыть все открытые файловые дескрипторы
    for (int i = 0; i < file_descriptor_capacity; i++) {
        if (file_descriptors[i] != NULL) {
            ufs_close(i);
        }
    }

    // Удалить все файлы из списка файлов
    struct file *current_file = file_list;
    while (current_file != NULL) {
        struct file *next_file = current_file->next;
        deleteFile(current_file);
        current_file = next_file;
    }
    
    // Освободить память, занимаемую массивом file_descriptors
	for(int i = 0; i < file_descriptor_capacity; i++) {
    	free(file_descriptors[i]);
	}

    file_descriptor_count = 0;
    file_descriptor_capacity = 0;
	
	free(file_descriptors);
    if (file_list != NULL) free(file_list);
}

#ifdef NEED_RESIZE

/**
 * Resize a file opened by the file descriptor @a fd. If current
 * file size is less than @a new_size, then new empty blocks are
 * created and positions of opened file descriptors are not
 * changed. If the current size is bigger than @a new_size, then
 * the blocks are truncated. Opened file descriptors behind the
 * new file size should proceed from the new file end.
 **/
int
ufs_resize(int fd, size_t new_size){
	struct filedesc *file_desc = file_descriptors[fd];
	if (file_desc == NULL || file_desc->file == NULL){
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}
	
	struct file *file = file_desc->file;

	if (new_size == file->file_size){ return 0;}
	if (new_size > file->file_size){ //add empty blocks
		if (new_size > MAX_FILE_SIZE){
		ufs_error_code = UFS_ERR_NO_MEM;
		return -1;
		}

		size_t bytes_to_add = new_size - file->file_size;
		if (bytes_to_add >= BLOCK_SIZE - file->last_block->occupied){
			bytes_to_add = bytes_to_add - (BLOCK_SIZE - file->last_block->occupied);
			file->last_block->occupied = BLOCK_SIZE;
		}
		while (bytes_to_add > 0){
            ////////////////////
			struct block *new_block = (struct block *) calloc(sizeof(struct block), 1);
            if (new_block == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                break;
            } 

            new_block->memory = (char *) calloc(sizeof(char), BLOCK_SIZE);
            file_desc->block_position = 0;

            if (new_block->memory == NULL) {
                free(new_block);
                ufs_error_code = UFS_ERR_NO_MEM;
                break;
            }

            new_block->occupied = 0;
            file_desc->block = new_block;
            file_desc->block->prev = file->last_block;
            file_desc->block->next = NULL;

            struct block *tail = file->last_block;

            if(tail != NULL){
                tail->next = file_desc->block;
                file_desc->block->prev = tail;
            }
            
            file->last_block = file_desc->block;
            if(file->block_list == NULL){
                file->block_list = file_desc->block;
            }

            //////////////////
			if (bytes_to_add >= BLOCK_SIZE - file->last_block->occupied){
				bytes_to_add = bytes_to_add - (BLOCK_SIZE - file->last_block->occupied);
				file->last_block->occupied = BLOCK_SIZE;
			}
			else{
				file->last_block->occupied = bytes_to_add;
				bytes_to_add = 0;
			}
		}
	}
	else{ //cut existed blocks
		struct block *current = file->last_block;
		size_t bytes_to_cut = file->file_size - new_size;
		while (bytes_to_cut > 0 && current != NULL){
			if (bytes_to_cut >= current->occupied){
				bytes_to_cut = bytes_to_cut - current->occupied;
				current->occupied = 0;
				current = current->prev;
				if (current != NULL) {
					free(current->next->memory);
					free(current->next);
				}
				current->next = NULL;
			}
			else{
				current->occupied = current->occupied - bytes_to_cut;
				bytes_to_cut = 0;
			}
		}
		file->file_size = new_size;
		file->last_block = current;
	}
	/*struct filedesc *current_file_desc = *file_descriptors;
	while (current_file_desc != NULL){
		if (current_file_desc->file == file){
			current_file_desc->block = file->last_block;
			current_file_desc->block_position = file->last_block->occupied;
		}
		//current_file_desc = current_file_desc->next;
	}*/
	file_desc->block = file->block_list;
	file_desc->block_position = 0;
	return 0;
}

#endif
