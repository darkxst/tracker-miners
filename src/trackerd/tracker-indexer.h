/* Tracker - indexer and metadata database engine
 * Copyright (C) 2006, Mr Jamie McCracken (jamiemcc@gnome.org)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */



#ifndef _TRACKER_INDEXER_H
#define _TRACKER_INDEXER_H


#include <stdlib.h>
#include <glib.h>

#include "tracker-utils.h"

typedef struct {                         	 
	guint32 	service_id;              /* Service ID of the document */
	guint32 	service_type_id;         /* Service type ID of the document */
	guint32 	score;            	 /* Ranking score */
} SearchHit;



guint32		tracker_indexer_calc_amalgamated 	(int service, int score);
void		tracker_index_free_hit_list		(GSList *hit_list);

Indexer * 	tracker_indexer_open 			(const char *name);
void		tracker_indexer_close 			(Indexer *indexer);
gboolean	tracker_indexer_optimize		(Indexer *indexer);
void		tracker_indexer_sync 			(Indexer *indexer);

/* Indexing api */
gboolean	tracker_indexer_append_word_chunk 	(Indexer *indexer, const char *word, WordDetails *details, int word_detail_count);
gboolean	tracker_indexer_append_word 		(Indexer *indexer, const char *word, guint32 id, int service, int score);
gboolean	tracker_indexer_update_word 		(Indexer *indexer, const char *word, guint32 id, int service, int score, gboolean remove_word);

/* returns a GSList containing SearchHit structs */
GSList *	tracker_indexer_get_hits (Indexer *indexer, char **words, int *service_array,  int array_count, int offset, int limit, gboolean get_count, int *total_count);


char ***	tracker_get_hit_counts 		(Indexer *indexer, char **words);


#endif
