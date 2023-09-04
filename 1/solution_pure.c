#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libcoro.h"

#define MAX_NUMBERS 10000

struct context {
    char* filename;
    int (*numbers)[MAX_NUMBERS];
    int* num_elements;
    long whenStarted; // в какой момент времени корутина запустилась/продолжила работу
    long time; // сколько проработала
    int amount_of_switches;
};

void swap(int* a, int* b) {
    int temp = *a;
    *a = *b;
    *b = temp;
}


// Function to partition the array for Quick Sort
int partition(int arr[], int low, int high) {
    int pivot = arr[high];
    int i = low - 1;

    for (int j = low; j < high; j++) {
        if (arr[j] <= pivot) {
            i++;
            swap(&arr[i], &arr[j]);
        }
    }

    swap(&arr[i + 1], &arr[high]);
    return i + 1;
}

void quick_sort(int arr[], int low, int high, struct context* context) {
    if (low < high) {
        int pi = partition(arr, low, high);

        quick_sort(arr, low, pi - 1, context);
        quick_sort(arr, pi + 1, high, context);
    }

    // посчитать, сколько времени проработала корутина
    struct timespec currentTime;
    clock_gettime(CLOCK_MONOTONIC, &currentTime);

    long currentTimeAll = currentTime.tv_sec * 1000000000 + currentTime.tv_nsec;
    long elapsedTimeNSec = currentTimeAll - context->whenStarted;

    coro_yield();
    context->amount_of_switches++;
    
    clock_gettime(CLOCK_MONOTONIC, &currentTime);
    currentTimeAll = currentTime.tv_sec * 1000000000 + currentTime.tv_nsec;

    context->time += elapsedTimeNSec;
    // записать, в какое время корутина продолжила работу
    context->whenStarted = currentTimeAll;
    
}

// Function to read numbers from file and store them in an array
int read_numbers(const char* filename, int arr[]) {
    FILE* file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    int number;
    int index = 0;
    while (fscanf(file, "%d", &number) != EOF && index < MAX_NUMBERS) {
        arr[index++] = number;
    }

    fclose(file);
    return index; // Return the number of elements read from the file
}

// Function to write sorted numbers to file
void write_numbers(const char* filename, int arr[], int n) {
    FILE* file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n; i++) {
        fprintf(file, "%d\n", arr[i]);
    }

    fclose(file);
}

void mergeFiles(int numbers[][MAX_NUMBERS], int num_elements[], int numFiles) {
	int merged[numFiles];
	memset(merged, 0, numFiles * sizeof(int));

    FILE *output = fopen("merged_sorted_output.txt", "w");
    if (!output) {
        perror("Ошибка при открытии файла");
        exit(1);
    }

    while (true) {
        int minIndex = -1;
		int minElement = INT_MAX;
        for (int i = 0; i < numFiles; i++) {
            if (merged[i] < num_elements[i] && numbers[i][merged[i]] < minElement) {
                minIndex = i;
				minElement = numbers[i][merged[i]];
            }
        }

        if (minIndex == -1) {
            break;
        }
		merged[minIndex]++;
		

        fprintf(output, "%d ", minElement);
    }

    fclose(output);
}

static int
coroutine_func_f(void *ctx)
{
    struct context* context = (struct context*)ctx;
    clock_gettime(CLOCK_MONOTONIC, (struct timespec *)&context->whenStarted);// записать, когда корутина начала работу

    *context->num_elements = read_numbers(context->filename, *context->numbers);
        
    quick_sort(*context->numbers, 0, *context->num_elements, context);

    // посчитать, скольк корутина проработала
    struct timespec currentTime;
    clock_gettime(CLOCK_MONOTONIC, &currentTime);
    context->time += currentTime.tv_sec * 1000000000 + currentTime.tv_nsec - context->whenStarted;
	return 0;
}

int main(int argc, char **argv)
{
	struct timespec startTime;
    clock_gettime(CLOCK_MONOTONIC, &startTime);

    coro_sched_init();
    int numFiles = argc - 1;

	int numbers[numFiles][MAX_NUMBERS];
	int num_elements[numFiles];

    struct context contexts[numFiles];
   // struct coro *coroutines[numFiles]; // Массив для хранения корутин

    
	if (argc > 1) {
        for (int i = 0; i < numFiles; i++) {
            contexts[i].filename = argv[i + 1];
            contexts[i].numbers = &numbers[i];
            contexts[i].num_elements = &num_elements[i];
            contexts[i].time = 0;
            contexts[i].amount_of_switches = 0;

            coro_new(coroutine_func_f, &contexts[i]);
	   } 
	} else {
        printf("Аргументы не введены.\n");
    	}

    struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		coro_delete(c);
	}

    // Вывод времени работы каждой корутины
    for (int i = 0; i < numFiles; i++) {
        struct context *ctx = &contexts[i]; // Получаем указатель на контекст из массива
        long coroutineTime = ctx->time;
        int switchCount = ctx-> amount_of_switches;
        printf("Finished, Время работы корутины %d: %ld ns\n", i, coroutineTime);
        printf("Finished, Количество переключений корутины %d: \n", switchCount);

        //coro_delete(coroutines[i]); // Освобождаем память, выделенную для корутины
    }

    mergeFiles(numbers, num_elements, numFiles);

    struct timespec endTime;
    clock_gettime(CLOCK_MONOTONIC, &endTime);

    long totalTimeNs = (endTime.tv_sec - startTime.tv_sec) * 1000000000 + (endTime.tv_nsec - startTime.tv_nsec);
    double totalTimeSec = totalTimeNs / 1e9;

    printf("Суммарное время работы программы: %.3lf секунд\n", totalTimeSec);



	return 0;
}