//  GREKO Chess Engine
//  (c) 2002-2013 Vladimir Medvedev <vrm@bk.ru>
//  http://greko.110mb.com

//  defaults.h: default values for configurable engine parameters
//  modified: 30-June-2013

#ifndef DEFAULTS_H
#define DEFAULTS_H

#define DEFAULT_HASH_MB                   32
#define DEFAULT_MULTI_PV                   1
#define DEFAULT_UCI_LIMIT_STRENGTH   "false"
#define DEFAULT_UCI_ELO                 2000
#define DEFAULT_LIMIT_KNPS              9999
#define DEFAULT_MATERIAL                  50
#define DEFAULT_BOARD_CONTROL             50
#define DEFAULT_MOBILITY                  50
#define DEFAULT_PAWN_STRUCT               50
#define DEFAULT_PAWN_PASSED               50
#define DEFAULT_KING_SAFETY               50
#define DEFAULT_LAZY_EVAL_MARGIN         400
#define DEFAULT_DRAW_SCORE                 0
#define DEFAULT_NULL_MOVE_REDUCTION        3
#define DEFAULT_NULL_MOVE_MIN_DEPTH        2
#define DEFAULT_PRUNING_MARGIN_1          50
#define DEFAULT_PRUNING_MARGIN_2         350
#define DEFAULT_PRUNING_MARGIN_3         550
#define DEFAULT_LMR_MIN_DEPTH              3
#define DEFAULT_LMR_MIN_MOVE_NUMBER        4

#endif
