/*
 * dragon_tbb.c
 *
 *  Created on: 2011-08-17
 *      Author: Francis Giraldeau <francis.giraldeau@gmail.com>
 */

#include <iostream>

extern "C" {
#include "dragon.h"
#include "color.h"
#include "utils.h"
}
#include "dragon_tbb.h"
#include "tbb/tbb.h"
#include "TidMap.h"

using namespace std;
using namespace tbb;

class DragonLimits {
	public:
		DragonLimits(){	//Boring constructor
			piece_init(&mpiece);
		}
		DragonLimits(const DragonLimits& dragon, split){ //Splitting constructor
			piece_init(&mpiece);
		}
		void operator()(const blocked_range<uint64_t>& range){
			piece_limit(range.begin(),range.end(),&mpiece);
		}
		void join(DragonLimits& dragon){
			piece_merge(&mpiece,dragon.mpiece);
		}
		piece_t& getPiece() {return mpiece;}
		piece_t mpiece;
};

class DragonDraw {
	public:
		DragonDraw(struct draw_data* data){
			mdata = data;
		}
		DragonDraw(const DragonDraw& dragon){
			mdata = dragon.mdata;
		}
		void operator()(const blocked_range<int>& range) const{
			int indexBegin = ((range.begin() * mdata->nb_thread) / (mdata->size));
			int indexEnd = ((range.end() * mdata->nb_thread) / mdata->size);
			if(indexBegin != indexEnd)
			{
				int begin1 = range.begin();
				int end1 = (indexEnd * mdata->size / mdata->nb_thread) - 1;
				int begin2 = end1 + 1;
				int end2 = range.end();
				dragon_draw_raw(begin1,end1,mdata->dragon, mdata->dragon_width, mdata->dragon_height,mdata->limits, indexBegin);
				dragon_draw_raw(begin2,end2,mdata->dragon, mdata->dragon_width, mdata->dragon_height,mdata->limits, indexEnd);
			}
			else
			{
				dragon_draw_raw(range.begin(),range.end(),mdata->dragon, mdata->dragon_width, mdata->dragon_height,mdata->limits, indexBegin);
			}
			
				
		}
		struct draw_data* mdata;
};

class DragonRender {
	public:
		DragonRender(struct draw_data* data){
			mdata = data;
		}
		DragonRender(const DragonRender& dragon){
			mdata = dragon.mdata;
		}
		void operator()(const blocked_range<int>& range) const{
			scale_dragon(range.begin(),range.end(),mdata->image,mdata->image_width,mdata->image_height, mdata->dragon, mdata->dragon_width, mdata->dragon_height, mdata->palette);
		}
	struct draw_data* mdata;
};

class DragonClear {
	 public:
	 DragonClear(char initValue, char* canvas){
		 mdefaultValue = initValue;
		 mcanvas = canvas;
	 }
	 DragonClear(const DragonClear& dragon){
		 mdefaultValue = dragon.mdefaultValue;
		 mcanvas = dragon.mcanvas;
	 }
	 void operator()(const blocked_range<int>& range) const{
		 init_canvas(range.begin(),range.end(),mcanvas, mdefaultValue);
	 }
		char mdefaultValue;
		char* mcanvas;
};

int dragon_draw_tbb(char **canvas, struct rgb *image, int width, int height, uint64_t size, int nb_thread)
{
	struct draw_data data;
	limits_t limits;
	char *dragon = NULL;
	int dragon_width;
	int dragon_height;
	int dragon_surface;
	int scale_x;
	int scale_y;
	int scale;
	int deltaJ;
	int deltaI;

	struct palette *palette = init_palette(nb_thread);
	if (palette == NULL)
		return -1;

	/* 1. Calculer les limites du dragon */
	dragon_limits_tbb(&limits, size, nb_thread);

	dragon_width = limits.maximums.x - limits.minimums.x;
	dragon_height = limits.maximums.y - limits.minimums.y;
	dragon_surface = dragon_width * dragon_height;
	scale_x = dragon_width / width + 1;
	scale_y = dragon_height / height + 1;
	scale = (scale_x > scale_y ? scale_x : scale_y);
	deltaJ = (scale * width - dragon_width) / 2;
	deltaI = (scale * height - dragon_height) / 2;

	dragon = (char *) malloc(dragon_surface);
	if (dragon == NULL) {
		free_palette(palette);
		*canvas = NULL;
		return -1;
	}

	data.nb_thread = nb_thread;
	data.dragon = dragon;
	data.image = image;
	data.size = size;
	data.image_height = height;
	data.image_width = width;
	data.dragon_width = dragon_width;
	data.dragon_height = dragon_height;
	data.limits = limits;
	data.scale = scale;
	data.deltaI = deltaI;
	data.deltaJ = deltaJ;
	data.palette = palette;

	task_scheduler_init init(nb_thread);

	/* 2. Initialiser la surface : DragonClear */
	DragonClear dragonClear(-1,dragon);
	parallel_for(blocked_range<int>(0,dragon_surface),dragonClear);
	/* 3. Dessiner le dragon : DragonDraw */
	DragonDraw dragonDraw(&data);
	parallel_for(blocked_range<int>(0,data.size),dragonDraw);
	/* 4. Effectuer le rendu final : DragonRender */
	DragonRender dragonRender(&data);
	parallel_for(blocked_range<int>(0,data.image_height),dragonRender);

	init.terminate();
	free_palette(palette);
	*canvas = dragon;
	return 0;
}

/*
 * Calcule les limites en terme de largeur et de hauteur de
 * la forme du dragon. Requis pour allouer la matrice de dessin.
 */
int dragon_limits_tbb(limits_t *limits, uint64_t size, int nb_thread)
{
	DragonLimits lim;
	task_scheduler_init task(nb_thread);
	parallel_reduce(blocked_range<uint64_t>(0,size),lim);
	piece_t piece = lim.getPiece();
	*limits = piece.limits;
	return 0;
}
