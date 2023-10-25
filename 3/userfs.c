#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "userfs.h"
#include <stddef.h>
#include <string.h>
enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 100,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_error_code = UFS_ERR_NO_ERR;

struct block {
	/** Block memory. */
	char memory[BLOCK_SIZE];
	/** How many bytes are occupied. */
	int occupied;
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

	/* PUT HERE OTHER MEMBERS */
};

/** List of all files. */
static struct file *file_list = NULL;

struct filedesc {
	struct file *file;//открытый файл
//позиция в открытом файле,лучше хранить ссылку на блок и позиция внутри блока
    void *block;        // Pointer to the current block being accessed
    size_t block_position; 

	/* PUT HERE OTHER MEMBERS */
};

/**
 * An array of file descriptors. When a file descriptor is
 * created, its pointer drops here. When a file descriptor is
 * closed, its place in this array is set to NULL and can be
 * taken by next ufs_open() call.

 были 0, 1, 2
 елси закрыли 1, то следующий открытый дескриптор - 1 
 ищем свободное место с минимальным индексом и возвращаем индекс 

 */


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
    file_descriptor_count++;

    return new_index; // Возвращаем индекс выделенного дескриптора.
}

//если налл - нет дескриптора, если не нал есть
//если активных дескрипторов больше capacity - error
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
           	return allocate_file_descriptor(file); // Возвращаем дескриптор существующего файла.
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
		ufs_error_code = UFS_ERR_NO_ERR;
		return fd;
    	
	}else{
		ufs_error_code = UFS_ERR_NO_FILE;
		return -1;
	}

    

    
}

ssize_t ufs_write(int fd, const char *buf, size_t size) {
    // подразумеваем, что buff != NULL всегда

    // Поиск файла по дескриптору
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];
    struct file *file = file_desc->file;

    // Запись данных в файл
    size_t bytes_written = 0;
    while (bytes_written < size) {
        struct block *current_block = file->last_block;
        
        if (current_block == NULL || current_block->occupied == BLOCK_SIZE) {
            // Создаем новый блок, если текущего нет или он полный
            struct block *new_block = (struct block *)malloc(sizeof(struct block));
            if (new_block == NULL) {
                ufs_error_code = UFS_ERR_NO_MEM;
                return -1;
            }
            new_block->occupied = 0;
            new_block->next = NULL;
            new_block->prev = current_block;
            if (current_block != NULL) {
                current_block->next = new_block;
            }
            if (file->block_list == NULL) {
                file->block_list = new_block;
            }
            file->last_block = new_block;
            current_block = new_block;
        }

        // Определяем, сколько байт можно записать в текущий блок
        size_t available_space = BLOCK_SIZE - current_block->occupied;
        size_t bytes_to_write = (size - bytes_written < available_space) ? (size - bytes_written) : available_space;

        // Копируем данные из буфера в блок
        memcpy(current_block->memory + current_block->occupied, buf + bytes_written, bytes_to_write);
        current_block->occupied += bytes_to_write;
        bytes_written += bytes_to_write;
    }

    ufs_error_code = UFS_ERR_NO_ERR;
    return bytes_written;
}

//сделать проверку на размер файла при желаемом размере записываемого файла 

ssize_t ufs_read(int fd, char *buf, size_t size) {
    // Check if the file descriptor is valid
    if (fd < 0 || fd >= file_descriptor_capacity || file_descriptors[fd] == NULL) {
        ufs_error_code = UFS_ERR_NO_FILE;
        return -1;
    }

    struct filedesc *file_desc = file_descriptors[fd];
    struct file *file = file_desc->file;

    // Retrieve the current block and position from the file descriptor
    struct block *current_block = file_desc->block;
    size_t current_block_position = file_desc->block_position;

    // If the current_block is NULL, it means the file was just opened. 
    // In this case, set the current_block to the first block.
    if (current_block == NULL) {
        current_block = file->block_list;
    }

    size_t bytes_read = 0;

    // Skip already read bytes
    while (bytes_read < size && current_block != NULL && current_block_position > 0) {
        size_t available_data = current_block->occupied - current_block_position;
        size_t bytes_to_skip = (size - bytes_read < available_data) ? (size - bytes_read) : available_data;
        current_block_position += bytes_to_skip;
        bytes_read += bytes_to_skip;
    }

    while (bytes_read < size && current_block != NULL) {
        size_t available_data = current_block->occupied - current_block_position;
        size_t bytes_to_read = (size - bytes_read < available_data) ? (size - bytes_read) : available_data;

        // Copy data from the current block to the buffer
        memcpy(buf + bytes_read, current_block->memory + current_block_position, bytes_to_read);
        bytes_read += bytes_to_read;

        current_block_position += bytes_to_read;

        if (current_block_position == (size_t)current_block->occupied) {
            // Move to the next block
            current_block = current_block->next;
            current_block_position = 0;
        }
    }

    // Update the file descriptor with the current block and position
    file_desc->block = current_block;
    file_desc->block_position = current_block_position;

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
	printf("File Descriptor: %d \n", fd);
	printf("File Descriptor Capacity: %d \n", file_descriptor_capacity);

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
			} else {
    			file_list = file->prev;
			}
			if (file->prev != NULL) {
    			file->prev->next = file->next;
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

    file_descriptor_count = 0;
    file_descriptor_capacity = 0;

	// Освободить память, занимаемую массивом file_descriptors
	for(int i = 0; i < file_descriptor_capacity; i++) {
    	free(file_descriptors[i]);
	}
	
	free(file_descriptors);
    file_list = NULL;
}
//закрываем все открытые дескрипторы
//удаляем все файлы
//при удалении освобождаем все блоки
//в destroy используем delete close