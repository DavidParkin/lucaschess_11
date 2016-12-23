
// sort.cpp

// includes

//#include "attack.h"
#include "board.h"
//#include "colour.h"
//#include "list.h"
#include "move.h"
#include "move_check.h"
#include "move_evasion.h"
#include "move_gen.h"
#include "move_legal.h"
//#include "piece.h"
#include "see.h"
#include "sort.h"
#include "util.h"
#include "value.h"

// macros

#define HISTORY_INC(depth) ((depth)*(depth))

// constants

static const int KillerNb = 2;

static const int TransScore   = +32766;
static const int GoodScore    =  +4000;
static const int KillerScore  =     +4;
static const int HistoryScore = -24000;
static const int BadScore     = -28000;

static const int CODE_SIZE = 256;

// WHM NodePV capture_is_good() additions: 
static const bool TryPasserSacs          =  true;
static const bool TryKingAttackSacs      = false;
static const bool TryKingBoxSacs         = false;
static const bool TryNodePVQueenPromotes = false;

// WHM NodePV note_quiet_moves() additions: 
static const bool TryQuietKingAttacks = false;


// types

enum gen_t {
   GEN_ERROR,
   GEN_LEGAL_EVASION,
   GEN_TRANS,
   GEN_GOOD_CAPTURE,
   GEN_BAD_CAPTURE,
   GEN_KILLER,
   GEN_QUIET,
   GEN_EVASION_QS,
   GEN_CAPTURE_QS,
   GEN_CHECK_QS,
   GEN_END
};

enum test_t {
   TEST_ERROR,
   TEST_NONE,
   TEST_LEGAL,
   TEST_TRANS_KILLER,
   TEST_GOOD_CAPTURE,
   TEST_BAD_CAPTURE,
   TEST_KILLER,
   TEST_QUIET,
   TEST_CAPTURE_QS,
   TEST_CHECK_QS
};

// variables

static int PosLegalEvasion;
static int PosSEE;

static int PosEvasionQS;
static int PosCheckQS;
static int PosCaptureQS;

static int Code[CODE_SIZE];

static          unsigned short Killer[MaxThreads][HeightMax][KillerNb];

static volatile uint32 History[HistorySize];
       volatile uint32 HistHit[HistorySize];
       volatile uint32 HistTot[HistorySize];

// prototypes

static void note_captures     (list_t * list, const board_t * board);
static void note_quiet_moves  (list_t * list, const board_t * board, bool in_pv, int ThreadId);
static void note_moves_simple (list_t * list, const board_t * board);
static void note_mvv_lva      (list_t * list, const board_t * board);

static int  move_value             (int move, const board_t * board, int height, int trans_killer, int ThreadId);
static int  capture_value          (int move, const board_t * board);
static int  quiet_move_value       (int move, const board_t * board, int ThreadId);
static int  move_value_simple      (int move, const board_t * board);

static int  history_prob           (int move, const board_t * board, int ThreadId);

#if !DEBUG
static bool capture_is_good        (int move, const board_t * board, bool in_pv);
#endif

static int  mvv_lva                (int move, const board_t * board);

static unsigned int  history_index (int move, const board_t * board);

// functions

// sort_init()

void sort_init() {

   int pos;
   int ThreadId, i, height;

   // killer
   
   for (ThreadId = 0; ThreadId < NumberThreadsInternal; ThreadId++) {
      for (height = 0; height < HeightMax; height++) {
         for (i = 0; i < KillerNb; i++) Killer[ThreadId][height][i] = MoveNone;
      }
   }

   // history
   
   for (i = 0; i < HistorySize; i++) {
      History[i] = 0; // good_move
      HistHit[i] = 1;
      HistTot[i] = 1;
   }

   // Code[]

   for (pos = 0; pos < CODE_SIZE; pos++) Code[pos] = GEN_ERROR;

   pos = 0;

   // main search

   PosLegalEvasion = pos;
   Code[pos++] = GEN_LEGAL_EVASION;
   Code[pos++] = GEN_END;

   PosSEE = pos;
   Code[pos++] = GEN_TRANS;
   Code[pos++] = GEN_GOOD_CAPTURE;
   Code[pos++] = GEN_KILLER;
   Code[pos++] = GEN_QUIET;
   Code[pos++] = GEN_BAD_CAPTURE;
   Code[pos++] = GEN_END;

   // quiescence search

   PosEvasionQS = pos;
   Code[pos++] = GEN_EVASION_QS;
   Code[pos++] = GEN_END;

   PosCheckQS = pos;
   Code[pos++] = GEN_CAPTURE_QS;
   Code[pos++] = GEN_CHECK_QS;
   Code[pos++] = GEN_END;

   PosCaptureQS = pos;
   Code[pos++] = GEN_CAPTURE_QS;
   Code[pos++] = GEN_END;

   ASSERT(pos<CODE_SIZE);
}

// sort_init()

void sort_init(sort_t * sort, board_t * board, const attack_t * attack, int depth, int height, int trans_killer, bool in_pv, int ThreadId) {

   ASSERT(sort!=NULL);
   ASSERT(board!=NULL);
   ASSERT(attack!=NULL);
   ASSERT(depth_is_ok(depth));
   ASSERT(height_is_ok(height));
   ASSERT(trans_killer==MoveNone||move_is_ok(trans_killer));

   sort->board = board;
   sort->attack = attack;

   sort->height = height;

   sort->trans_killer = trans_killer;
   sort->killer_1 = Killer[ThreadId][sort->height][0]; // last best
   sort->killer_3 = Killer[ThreadId][sort->height][1];
   if (sort->height > 2){
      sort->killer_2 = Killer[ThreadId][sort->height-2][0];  // last best below
      sort->killer_4 = Killer[ThreadId][sort->height-2][1];
   }
   else{
      sort->killer_2 = MoveNone;
      sort->killer_4 = MoveNone;
   }

   // uniqueness added.                                                // WHM
   if (sort->killer_1 != MoveNone) {                                   // WHM
      if (sort->killer_2 == sort->killer_1) sort->killer_2 = MoveNone; // WHM
      if (sort->killer_3 == sort->killer_1) sort->killer_3 = MoveNone; // WHM
      if (sort->killer_4 == sort->killer_1) sort->killer_4 = MoveNone; // WHM
   }                                                                   // WHM
   if (sort->killer_2 != MoveNone) {                                   // WHM
      if (sort->killer_3 == sort->killer_2) sort->killer_3 = MoveNone; // WHM
      if (sort->killer_4 == sort->killer_2) sort->killer_4 = MoveNone; // WHM
   }                                                                   // WHM
      if (sort->killer_4 == sort->killer_3) sort->killer_4 = MoveNone; // WHM

   ASSERT(sort->killer_1 == MoveNone  ||  sort->killer_1 != sort->killer_2); // WHM
   ASSERT(sort->killer_1 == MoveNone  ||  sort->killer_1 != sort->killer_3); // WHM
   ASSERT(sort->killer_1 == MoveNone  ||  sort->killer_1 != sort->killer_4); // WHM
   ASSERT(sort->killer_2 == MoveNone  ||  sort->killer_2 != sort->killer_1); // WHM
   ASSERT(sort->killer_2 == MoveNone  ||  sort->killer_2 != sort->killer_3); // WHM
   ASSERT(sort->killer_2 == MoveNone  ||  sort->killer_2 != sort->killer_4); // WHM
   ASSERT(sort->killer_3 == MoveNone  ||  sort->killer_3 != sort->killer_1); // WHM
   ASSERT(sort->killer_3 == MoveNone  ||  sort->killer_3 != sort->killer_2); // WHM
   ASSERT(sort->killer_3 == MoveNone  ||  sort->killer_3 != sort->killer_4); // WHM
   ASSERT(sort->killer_4 == MoveNone  ||  sort->killer_4 != sort->killer_1); // WHM
   ASSERT(sort->killer_4 == MoveNone  ||  sort->killer_4 != sort->killer_2); // WHM
   ASSERT(sort->killer_4 == MoveNone  ||  sort->killer_4 != sort->killer_3); // WHM
   
   sort->in_pv = in_pv;
   
   if (ATTACK_IN_CHECK(sort->attack)) {

      gen_legal_evasions(sort->list,sort->board,sort->attack);
      note_moves(sort->list,sort->board,sort->height,sort->trans_killer,ThreadId);
      list_sort(sort->list);

      sort->gen = PosLegalEvasion + 1;
      sort->test = TEST_NONE;

   } else { // not in check

      LIST_CLEAR(sort->list);
      sort->gen = PosSEE;
   }

   sort->pos = 0;
}

// sort_next()

int sort_next(sort_t * sort, int ThreadId) {

   int move;
   int gen;

   ASSERT(sort!=NULL);

   while (true) {

      while (sort->pos < LIST_SIZE(sort->list)) {

         // next move

         move = LIST_MOVE(sort->list,sort->pos);
         sort->value = HistoryMax; // default score, HistoryMax instead of 16384
         sort->pos++;

         ASSERT(move!=MoveNone);

         // test

         if (false) {

         } else if (sort->test == TEST_NONE) {

            // no-op

         } else if (sort->test == TEST_TRANS_KILLER) {

            if (!move_is_pseudo(move,sort->board)) continue;
            if (!pseudo_is_legal(move,sort->board)) continue;

         } else if (sort->test == TEST_GOOD_CAPTURE) {

            ASSERT(move_is_tactical(move,sort->board));

            if (move == sort->trans_killer) continue;

            if (!capture_is_good(move,sort->board,sort->in_pv)) {
               LIST_ADD(sort->bad,move);
               continue;
            }

            if (!pseudo_is_legal(move,sort->board)) continue;
            
         } else if (sort->test == TEST_BAD_CAPTURE) {

            ASSERT(move_is_tactical(move,sort->board));
            ASSERT(!capture_is_good(move,sort->board,sort->in_pv));

            ASSERT(move!=sort->trans_killer);
            if (!pseudo_is_legal(move,sort->board)) continue;

            sort->value = HistoryBadCap; // WHM(31)

         } else if (sort->test == TEST_KILLER) {

            if (move == sort->trans_killer) continue;
            if (!quiet_is_pseudo(move,sort->board)) continue;
            if (!pseudo_is_legal(move,sort->board)) continue;

            ASSERT(!move_is_tactical(move,sort->board));

            sort->value = HistoryKiller; // WHM(31)

         } else if (sort->test == TEST_QUIET) {

            ASSERT(!move_is_tactical(move,sort->board));

            if (move == sort->trans_killer) continue;
            if (move == sort->killer_1) continue;
            if (move == sort->killer_2) continue;
            if (move == sort->killer_3) continue;
            if (move == sort->killer_4) continue;
            if (!pseudo_is_legal(move,sort->board)) continue;

            sort->value = history_prob(move,sort->board,ThreadId);

         } else {

            ASSERT(false);

            return MoveNone;
         }

         ASSERT(pseudo_is_legal(move,sort->board));

         return move;
      }

      // next stage

      gen = Code[sort->gen++];

      if (false) {

      } else if (gen == GEN_TRANS) {

         LIST_CLEAR(sort->list);
         if (sort->trans_killer != MoveNone) LIST_ADD(sort->list,sort->trans_killer);

         sort->test = TEST_TRANS_KILLER;

      } else if (gen == GEN_GOOD_CAPTURE) {

         gen_captures(sort->list,sort->board);
         note_mvv_lva(sort->list,sort->board);
         list_sort(sort->list);

         LIST_CLEAR(sort->bad);

         sort->test = TEST_GOOD_CAPTURE;

      } else if (gen == GEN_BAD_CAPTURE) {

         list_copy(sort->list,sort->bad);

         sort->test = TEST_BAD_CAPTURE;

      } else if (gen == GEN_KILLER) {

         LIST_CLEAR(sort->list);
         if (sort->killer_1 != MoveNone) LIST_ADD(sort->list,sort->killer_1);
         if (sort->killer_2 != MoveNone) LIST_ADD(sort->list,sort->killer_2);
         if (sort->killer_3 != MoveNone) LIST_ADD(sort->list,sort->killer_3);
         if (sort->killer_4 != MoveNone) LIST_ADD(sort->list,sort->killer_4);
         
         sort->test = TEST_KILLER;

      } else if (gen == GEN_QUIET) {

         gen_quiet_moves(sort->list,sort->board);
         note_quiet_moves(sort->list,sort->board,sort->in_pv,ThreadId);
         list_sort(sort->list);

         sort->test = TEST_QUIET;

      } else {

         ASSERT(gen==GEN_END);

         return MoveNone;
      }

      sort->pos = 0;
   }
}

// sort_init_qs()

void sort_init_qs(sort_t * sort, board_t * board, const attack_t * attack, bool check) {

   ASSERT(sort!=NULL);
   ASSERT(board!=NULL);
   ASSERT(attack!=NULL);
   ASSERT(check==true||check==false);

   sort->board = board;
   sort->attack = attack;

   if (ATTACK_IN_CHECK(sort->attack)) {
      sort->gen = PosEvasionQS;
   } else if (check) {
      sort->gen = PosCheckQS;
   } else {
      sort->gen = PosCaptureQS;
   }

   LIST_CLEAR(sort->list);
   sort->pos = 0;
}

// sort_next_qs()

int sort_next_qs(sort_t * sort) {

   int move;
   int gen;

   ASSERT(sort!=NULL);

   while (true) {

      while (sort->pos < LIST_SIZE(sort->list)) {

         // next move

         move = LIST_MOVE(sort->list,sort->pos);
         sort->pos++;

         ASSERT(move!=MoveNone);

         // test

         if (false) {

         } else if (sort->test == TEST_LEGAL) {

            if (!pseudo_is_legal(move,sort->board)) continue;

         } else if (sort->test == TEST_CAPTURE_QS) {

            ASSERT(move_is_tactical(move,sort->board));

            if (!capture_is_good(move,sort->board,false)) continue;
            if (!pseudo_is_legal(move,sort->board)) continue;

         } else if (sort->test == TEST_CHECK_QS) {

            ASSERT(!move_is_tactical(move,sort->board));
            ASSERT(move_is_check(move,sort->board));

            if (see_move(move,sort->board) < 0) continue;
            if (!pseudo_is_legal(move,sort->board)) continue;

         } else {

            ASSERT(false);

            return MoveNone;
         }

         ASSERT(pseudo_is_legal(move,sort->board));

         return move;
      }

      // next stage

      gen = Code[sort->gen++];

      if (false) {

      } else if (gen == GEN_EVASION_QS) {

         gen_pseudo_evasions(sort->list,sort->board,sort->attack);
         note_moves_simple(sort->list,sort->board);
         list_sort(sort->list);

         sort->test = TEST_LEGAL;

      } else if (gen == GEN_CAPTURE_QS) {

         gen_captures(sort->list,sort->board);
         note_mvv_lva(sort->list,sort->board);
         list_sort(sort->list);

         sort->test = TEST_CAPTURE_QS;

      } else if (gen == GEN_CHECK_QS) {

         gen_quiet_checks(sort->list,sort->board);

         sort->test = TEST_CHECK_QS;

      } else {

         ASSERT(gen==GEN_END);

         return MoveNone;
      }

      sort->pos = 0;
   }
}

// good_move()

void good_move(int move, const board_t * board, int depth, int height, int ThreadId) {

   unsigned int index;
   int i;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);
   ASSERT(depth_is_ok(depth));
   ASSERT(height_is_ok(height));


   if (move_is_tactical(move,board)) return;

   // killer

   if (Killer[ThreadId][height][0] != move) {
       Killer[ThreadId][height][1]  = Killer[ThreadId][height][0];
       Killer[ThreadId][height][0]  = move;
   }

   ASSERT(Killer[ThreadId][height][0]==move);
   ASSERT(Killer[ThreadId][height][1]!=move);

   // history

   index = history_index(move,board);

   History[index] += HISTORY_INC(depth);

   if (History[index] >= HistoryMax) { // HistoryMax instead of 16384
      for (i = 0; i < HistorySize; i++) {
         if (History[i] >= 2) {
             History[i] = (History[i] + 1) / 2;
         }
      }
   } 
}

// history_good()

void history_good(int move, const board_t * board, int ThreadId) {

   unsigned int index;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(!move_is_tactical(move,board));
// if (    move_is_tactical(move,board)) return;

   // history

   index = history_index(move,board);

   HistHit[index]++;
   HistTot[index]++;
   
   if (HistTot[index] >= HistTotMax) {
       HistHit[index] = (HistHit[index] + 1) / 2;
       HistTot[index] = (HistTot[index] + 1) / 2;
   }

   ASSERT(HistHit[index]<=HistTot[index]);
   ASSERT(HistTot[index]<HistTotMax);
}

// history_bad()

void history_bad(int move, const board_t * board, int ThreadId) {

   unsigned int index;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(!move_is_tactical(move,board));
// if (    move_is_tactical(move,board)) return;

   // history

   index = history_index(move,board);

   HistTot[index]++;
   
   if (HistTot[index] >= HistTotMax) {
       HistTot[index] = (HistTot[index] + 1) / 2;
       HistHit[index] = (HistHit[index] + 1) / 2;
   }

   ASSERT(HistHit[index]<=HistTot[index]);
   ASSERT(HistTot[index]<HistTotMax);
}

// note_moves()

void note_moves(list_t * list, const board_t * board, int height, int trans_killer, int ThreadId) {

   int size;
   int i, move;

   ASSERT(list_is_ok(list));
   ASSERT(board!=NULL);
   ASSERT(height_is_ok(height));
   ASSERT(trans_killer==MoveNone||move_is_ok(trans_killer));

   size = LIST_SIZE(list);

   if (size >= 2) {
      for (i = 0; i < size; i++) {
         move = LIST_MOVE(list,i);
         list->value[i] = move_value(move,board,height,trans_killer,ThreadId);
      }
   }
}

// note_captures()

static void note_captures(list_t * list, const board_t * board) {

   int size;
   int i, move;

   ASSERT(list_is_ok(list));
   ASSERT(board!=NULL);

   size = LIST_SIZE(list);

   if (size >= 2) {
      for (i = 0; i < size; i++) {
         move = LIST_MOVE(list,i);
         list->value[i] = capture_value(move,board);
      }
   }
}

// note_quiet_moves()

static void note_quiet_moves(list_t * list, const board_t * board, bool in_pv, int ThreadId) {

   int size;
   int i, move;
   int move_piece;

   ASSERT(list_is_ok(list));
   ASSERT(board!=NULL);

   size = LIST_SIZE(list);

   if (size >= 2) {
      for (i = 0; i < size; i++) {
         move = LIST_MOVE(list,i);
         list->value[i] = quiet_move_value(move,board,ThreadId);
         if (TryQuietKingAttacks && in_pv) {
            move_piece = MOVE_PIECE(move,board);
            if (!(PIECE_IS_PAWN(move_piece) || PIECE_IS_KING(move_piece))) {
               if (narrow_piece_attack_king(board,move_piece,MOVE_TO(move),KING_POS(board,COLOUR_OPP(board->turn)))) {
                  if (see_move(move,board) >= 0) {
//                   if (1 == NumberThreadsInternal) print_board(board);
                     list->value[i] += 16;
                  }
               }
            }
         }
      }
   }
}

// note_moves_simple()

static void note_moves_simple(list_t * list, const board_t * board) {

   int size;
   int i, move;

   ASSERT(list_is_ok(list));
   ASSERT(board!=NULL);

   size = LIST_SIZE(list);

   if (size >= 2) {
      for (i = 0; i < size; i++) {
         move = LIST_MOVE(list,i);
         list->value[i] = move_value_simple(move,board);
      }
   }
}

// note_mvv_lva()

static void note_mvv_lva(list_t * list, const board_t * board) {

   int size;
   int i, move;

   ASSERT(list_is_ok(list));
   ASSERT(board!=NULL);

   size = LIST_SIZE(list);

   if (size >= 2) {
      for (i = 0; i < size; i++) {
         move = LIST_MOVE(list,i);
         list->value[i] = mvv_lva(move,board);
      }
   }
}

// move_value()

static int move_value(int move, const board_t * board, int height, int trans_killer, int ThreadId) {

   int value;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);
   ASSERT(height_is_ok(height));
   ASSERT(trans_killer==MoveNone||move_is_ok(trans_killer));

   if (false) {
   } else if (move == trans_killer) { // transposition table killer
      value = TransScore;
   } else if (move_is_tactical(move,board)) { // capture or promote
      value = capture_value(move,board);
   } else if (move == Killer[ThreadId][height][0]) { // killer 1
      value = KillerScore;
   } else if (move == Killer[ThreadId][height][1]) { // killer 2
      value = KillerScore - 2;
   } else if (height > 2 && move == Killer[ThreadId][height-2][0]) { // killer 1
      value = KillerScore - 1;
   } else if (height > 2 && move == Killer[ThreadId][height-2][1]) { // killer 2
      value = KillerScore - 3;
   } else { // quiet move
      value = quiet_move_value(move,board,ThreadId);
   }

   return value;
}

// capture_value()

static int capture_value(int move, const board_t * board) {

   int value;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(move_is_tactical(move,board));

   value = mvv_lva(move,board);

   if (capture_is_good(move,board,false)) {
      value += GoodScore;
   } else {
      value += BadScore;
   }

   ASSERT(value>=-30000&&value<=+30000);

   return value;
}

// quiet_move_value()

static int quiet_move_value(int move, const board_t * board, int ThreadId) {

   int value;
   unsigned int index;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(!move_is_tactical(move,board));

   index = history_index(move,board);

   value = HistoryScore + History[index];
   ASSERT(value>=HistoryScore&&value<=KillerScore-4);

   return value;
}

// move_value_simple()

static int move_value_simple(int move, const board_t * board) {

   int value;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   value = HistoryScore;
   if (move_is_tactical(move,board)) value = mvv_lva(move,board);

   return value;
}

// history_prob()

static int history_prob(int move, const board_t * board, int ThreadId) {

   int value;
   unsigned int index;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(!move_is_tactical(move,board) || see_move(move,board) < 0 || move_is_under_promote(move));

   index = history_index(move,board);

   ASSERT(HistHit[index]<=HistTot[index]);
   ASSERT(HistTot[index]<HistTotMax);

   // int * int assumed to be int by C++, large values and overflows need to use double on the interim product.  
   value = int(double(double(HistHit[index]) * HistoryMax) / HistTot[index]);

// // for uint16 or short or unsigned short the simple formula is OK.  
// value =    (      (      (HistHit[index]) * HistoryMax) / HistTot[index]); // HistoryMax instead
   
   ASSERT(value>=1 && value<=16384);

   return value;
}

// capture_is_good()

#if DEBUG
       bool capture_is_good(int move, const board_t * board, bool in_pv) {
#else
static bool capture_is_good(int move, const board_t * board, bool in_pv) {
#endif

   int piece, capture;
   int see_value; // WHM 11/22/08

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(move_is_tactical(move,board));

   // special cases

   if (MOVE_IS_EN_PASSANT(move)) return true;
   if (move_is_under_promote(move)) return false; // REMOVE ME?  Keep, looks good to me.  WHM;
// if (MOVE_IS_PROMOTE(move)) return true; // WHM; promote-to-queen, measures a little weaker
//                                                 too many garbage lines going nuts.
   
   // captures and queen promotes
   capture = board->square[MOVE_TO(move)];
   piece = board->square[MOVE_FROM(move)];

   if (capture != Empty) {

      // capture

      ASSERT(move_is_capture(move,board));

      if (MOVE_IS_PROMOTE(move)) return true; // capture a piece on Rank8 and promote to queen
      
      if (VALUE_PIECE(capture) >= VALUE_PIECE(piece)) return true;
   }


// return see_move(move,board) >= 0; WHM 11/22/08

// WHM 11/22/08 START

   see_value = see_move(move,board);
   if (see_value >= 0) return true;

   if (TryNodePVQueenPromotes) {
      if (in_pv && MOVE_IS_PROMOTE(move)) {
          ASSERT(!move_is_under_promote(move));
          return true; // WHM:
      }
   }
   
   if (TryKingAttackSacs || TryKingBoxSacs || TryPasserSacs) {
      if (in_pv  &&  see_value > -ValueBishop  &&  capture != Empty) {
         ASSERT(COLOUR_IS(capture,COLOUR_OPP(board->turn)));
         // king attack sacs.  
         if (TryKingAttackSacs) {
            if (narrow_piece_attack_king(board, piece, MOVE_TO(move), KING_POS(board,COLOUR_OPP(board->turn)))) {
               return true;
            }
         }
         // sacrifice attacks around the narrow/close king box can be examined more fully.  Rybka lessons.  
         if (TryKingBoxSacs) {
            if (DISTANCE(MOVE_TO(move),KING_POS(board,COLOUR_OPP(board->turn))) <= 1) {
               return true;
            }
         }
         // passer sacrifices...
         if (TryPasserSacs) {
            if (PIECE_IS_PAWN(capture) && PAWN_RANK(MOVE_TO(move),COLOUR_OPP(board->turn)) >= Rank6) {
               return true;
            }
         }
      }
   } // WHM 11/22/08 END
   
   
   return false;
}

// mvv_lva()

static int mvv_lva(int move, const board_t * board) {

   int piece, capture, promote;
   int value;

   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);

   ASSERT(move_is_tactical(move,board));

   if (MOVE_IS_EN_PASSANT(move)) { // en-passant capture

      value = 5; // PxP

   } else if ((capture = board->square[MOVE_TO(move)]) != Empty) { // normal capture

      piece = board->square[MOVE_FROM(move)];

      value = PIECE_ORDER(capture) * 6 - PIECE_ORDER(piece) + 5;
      ASSERT(value>=0&&value<30);

   } else { // promote

      ASSERT(MOVE_IS_PROMOTE(move));

      promote = move_promote(move);

      value = PIECE_ORDER(promote) - 5;
      ASSERT(value>=-4&&value<0);
   }

   ASSERT(value>=-4&&value<+30);

   return value;
}

// history_index()

static unsigned int history_index(int move, const board_t * board) {

   unsigned int index;
   
   int move_from = MOVE_FROM(move);
   int piece_12  = PIECE_TO_12(board->square[move_from]);
   
   ASSERT(move_is_ok(move));
   ASSERT(board!=NULL);
   
   ASSERT(!move_is_tactical(move,board));
   
// index = PIECE_TO_12(board->square[MOVE_FROM(move)]) *     64                                       + SQUARE_TO_64(MOVE_TO(move)); fruit 2.1
// index = PIECE_TO_12(board->square[MOVE_FROM(move)]) * (64*64) + SQUARE_TO_64(MOVE_FROM(move)) * 64 + SQUARE_TO_64(MOVE_TO(move)); Toga II 1.2.1 (corrected?).  
   
   index = 64 * piece_12  +  SQUARE_TO_64(MOVE_TO(move)); // fruit 2.1 and Toga II 1.4beta5c
   
   
   ASSERT(index >= 0 && index < HistorySize);
   
   return index;
}

// end of sort.cpp