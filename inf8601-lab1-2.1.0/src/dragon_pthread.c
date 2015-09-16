/*
 * dragon_pthread.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>

#include "dragon.h"
#include "color.h"
#include "dragon_pthread.h"

pthread_mutex_t mutex_stdout;

void printf_safe(char *format, ...) {
	va_list ap;

	va_start(ap, format);
	pthread_mutex_lock(&mutex_stdout);
	vprintf(format, ap);
	pthread_mutex_unlock(&mutex_stdout);
	va_end(ap);
}

void *dragon_draw_worker(void *data) {
	struct draw_data* info = (struct draw_data*) data;
	
	/* 1. Initialiser la surface */
	uint64_t surface = info->dragon_width * info->dragon_height;
	uint64_t start = info->id*surface/info->nb_thread;
	uint64_t end = (info->id + 1)*surface/info->nb_thread;
	init_canvas(start,end,info->dragon, -1); 
	pthread_barrier_wait(info->barrier);

	/* 2. Dessiner le dragon */
	start = info->id * info->size / info->nb_thread;
	end = (info->id + 1) * info->size / info->nb_thread;
	dragon_draw_raw(start,end,info->dragon, info->dragon_width, info->dragon_height, info->limits, info->id);
	pthread_barrier_wait(info->barrier);
	
	/* 3. Effectuer le rendu final */
	start = info->id * info->image_height / info->nb_thread;
	end = (info->id + 1) * info->image_height / info->nb_thread;
	scale_dragon(start,end, info->image, info->image_width, info->image_height, info->dragon, info->dragon_width, info->dragon_height, info->palette);

	return NULL;
}

int dragon_draw_pthread(char **canvas, struct rgb *image, int width, int height,
		uint64_t size, int nb_thread) {
	pthread_t *threads = NULL;
	pthread_barrier_t barrier;
	limits_t limits;
	struct draw_data info;
	char *dragon = NULL;
	int scale_x;
	int scale_y;
	struct draw_data *data = NULL;
	struct palette *palette = NULL;
	int ret = 0;

	palette = init_palette(nb_thread);
	if (palette == NULL)
		goto err;

	if (pthread_barrier_init(&barrier, NULL, nb_thread) != 0) {
		printf("barrier init error\n");
		goto err;
	}

	/* 1. Calculer les limites du dragon */
	if (dragon_limits_pthread(&limits, size, nb_thread) < 0)
		goto err;

	info.dragon_width = limits.maximums.x - limits.minimums.x;
	info.dragon_height = limits.maximums.y - limits.minimums.y;

	if ((dragon = (char *) malloc(info.dragon_width * info.dragon_height))
			== NULL) {
		printf("malloc error dragon\n");
		goto err;
	}

	if ((data = malloc(sizeof(struct draw_data) * nb_thread)) == NULL) {
		printf("malloc error data\n");
		goto err;
	}

	if ((threads = malloc(sizeof(pthread_t) * nb_thread)) == NULL) {
		printf("malloc error threads\n");
		goto err;
	}

	info.image_height = height;
	info.image_width = width;
	scale_x = info.dragon_width / width + 1;
	scale_y = info.dragon_height / height + 1;
	info.scale = (scale_x > scale_y ? scale_x : scale_y);
	info.deltaJ = (info.scale * width - info.dragon_width) / 2;
	info.deltaI = (info.scale * height - info.dragon_height) / 2;
	info.nb_thread = nb_thread;
	info.dragon = dragon;
	info.image = image;
	info.size = size;
	info.limits = limits;
	info.barrier = &barrier;
	info.palette = palette;
	
	/* 2. Lancement du calcul parallèle principal avec draw_dragon_worker */
	int i = 0;
	for (i = 0; i < nb_thread; ++i) {
		data[i] = info;
		data[i].id = i;
		pthread_create(&threads[i], NULL, dragon_draw_worker, &data[i]);
	}
	/* 3. Attendre la fin du traitement */
	for (i = 0; i < nb_thread; ++i) {
		pthread_join(threads[i], NULL);
	}
	
	if (pthread_barrier_destroy(&barrier) != 0) {
		printf("barrier destroy error\n");
		goto err;
	}

	done: FREE(data);
	FREE(threads);
	free_palette(palette);
	*canvas = dragon;
	return ret;

	err: FREE(dragon);
	ret = -1;
	goto done;
}

void *dragon_limit_worker(void *data) {
	struct limit_data *lim = (struct limit_data *) data;
	piece_limit(lim->start, lim->end, &lim->piece);
	return NULL;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_pthread(limits_t *limits, uint64_t size, int nb_thread) {
	int ret = 0;
	pthread_t *threads = NULL;
	struct limit_data *thread_data = NULL;
	piece_t master;

	piece_init(&master);

	if ((threads = calloc(nb_thread, sizeof(pthread_t))) == NULL)
		goto err;

	if ((thread_data = calloc(nb_thread, sizeof(struct limit_data))) == NULL)
		goto err;

	/* 1. Lancement du calcul en parallèle avec dragon_limit_worker */
	int i = 0;
	for (i = 0; i < nb_thread; ++i) {
		thread_data[i].id = i;
		thread_data[i].start = i * size/nb_thread;
		thread_data[i].end = (i + 1) * size/nb_thread;
		thread_data[i].piece = master;
		pthread_create(&threads[i], NULL, dragon_limit_worker, &thread_data[i]);
	}
	/* 2. Attendre la fin du traitement */
	for (i = 0; i < nb_thread; ++i) {
		pthread_join(threads[i], NULL);
	}
	/* 3. Fusion des pièces */
	for( i = 0; i < nb_thread; ++i)
	{
		piece_merge(&master, thread_data[i].piece);
	}
	done: FREE(threads);
	FREE(thread_data);
	*limits = master.limits;
	return ret;
	err: ret = -1;
	goto done;
}
