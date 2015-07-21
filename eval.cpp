/*
 * Gunborg - UCI chess engine
 * Copyright (C) 2013-2015 Torbjörn Nilsson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * eval.cpp
 *
 *  Created on: Jan 18, 2014
 *      Author: Torbjörn Nilsson
 */

#include "board.h"
#include "eval.h"
#include "magic.h"
#include "moves.h"
#include <math.h>

namespace {

const int MAX_MATERIAL = 3100;

}

int square_proximity[64][64];

// piece square tables [WHITE|BLACK][64]
// values are set by init_eval()
int pawn_square_table[2][64];
int pawn_square_table_endgame[2][64];
int knight_square_table[2][64];
int bishop_square_table[2][64];
int rook_square_table[2][64];
int queen_square_table[2][64];
int king_square_table_endgame[2][64];

// returns the score from the playing side's perspective
int nega_evaluate(const Position& position, bool white_turn) {
	return white_turn ? evaluate(position) : -evaluate(position);
}

// score in centipawns
int evaluate(const Position& position) {
	uint64_t black_king = position.p[BLACK][KING];
	uint64_t white_king = position.p[WHITE][KING];
	if (black_king == 0) {
		return 10000;
	} else if (white_king == 0) {
		return -10000;
	}
	int black_king_square = lsb_to_square(black_king);
	int white_king_square = lsb_to_square(white_king);

	uint64_t black_squares = position.p[BLACK][KING] | position.p[BLACK][PAWN] | position.p[BLACK][KNIGHT]
			| position.p[BLACK][BISHOP] | position.p[BLACK][ROOK] | position.p[BLACK][QUEEN];

	uint64_t white_squares = position.p[WHITE][KING] | position.p[WHITE][PAWN] | position.p[WHITE][KNIGHT]
			| position.p[WHITE][BISHOP] | position.p[WHITE][ROOK] | position.p[WHITE][QUEEN];

	uint64_t occupied_squares = black_squares | white_squares;

	int white_piece_material = pop_count(position.p[WHITE][QUEEN]) * 900
			+ pop_count(position.p[WHITE][ROOK]) * 500
			+ pop_count(position.p[WHITE][BISHOP]) * 300
			+ pop_count(position.p[WHITE][KNIGHT]) * 300;

	int black_piece_material = pop_count(position.p[BLACK][QUEEN]) * 900
			+ pop_count(position.p[BLACK][ROOK]) * 500
			+ pop_count(position.p[BLACK][BISHOP]) * 300
			+ pop_count(position.p[BLACK][KNIGHT]) * 300;

	int total_material = white_piece_material + black_piece_material;
	if (total_material <= 300 && (position.p[WHITE][PAWN] | position.p[BLACK][PAWN]) == 0) {
		return 0; // draw by insufficient mating material
	}
	int score = 0;

	uint64_t white_pawn_protection_squares = ((position.p[WHITE][PAWN] & ~A_FILE) << 7)
			| ((position.p[WHITE][PAWN] & ~H_FILE) << 9);
	uint64_t black_pawn_protection_squares = ((position.p[BLACK][PAWN] & ~A_FILE) >> 9)
			| ((position.p[BLACK][PAWN] & ~H_FILE) >> 7);

	// The idea is if a white pawn is on any of these squares then it is not a passed pawn
	uint64_t black_pawn_blocking_squares = south_fill((position.p[BLACK][PAWN] >> 8) | black_pawn_protection_squares);
	uint64_t white_pawn_blocking_squares = north_fill((position.p[WHITE][PAWN] << 8) | white_pawn_protection_squares);

	uint64_t white_pawn_files = file_fill(position.p[WHITE][PAWN]);
	uint64_t black_pawn_files = file_fill(position.p[BLACK][PAWN]);

	uint64_t open_files = ~(white_pawn_files | black_pawn_files);

	uint64_t white_semi_open_files = ~white_pawn_files & black_pawn_files;
	uint64_t black_semi_open_files = ~black_pawn_files & white_pawn_files;

	uint64_t white_double_pawn_mask = north_fill(position.p[WHITE][PAWN] << 8);
	uint64_t black_double_pawn_mask = south_fill(position.p[BLACK][PAWN] >> 8);

	uint64_t white_pawns = position.p[WHITE][PAWN];

	uint64_t white_passed_pawns = ~black_pawn_blocking_squares & white_pawns;
	score += pop_count(white_passed_pawns) * PASSED_PAWN_BONUS * (MAX_MATERIAL - total_material) / MAX_MATERIAL;

	uint64_t white_doubled_pawns = white_double_pawn_mask & white_pawns;
	score -= pop_count(white_doubled_pawns) * DOUBLED_PAWN_PENALTY;

	uint64_t white_isolated_pawns = white_pawns & ~file_fill(white_pawn_protection_squares);
	score -= pop_count(white_isolated_pawns) * ISOLATED_PAWN_PENALTY;

	// a backward pawn cannot advance without being taken by opponent's pawn
	uint64_t black_dominated_stop_squares = ~north_fill(white_pawn_protection_squares) & black_pawn_protection_squares;
	uint64_t white_backward_pawns =  south_fill(black_dominated_stop_squares) & white_pawns;
	score -= pop_count(white_backward_pawns) * BACKWARD_PAWN_PENALTY;

	while (white_pawns) {
		int i = lsb_to_square(white_pawns);
		score += pawn_square_table_endgame[WHITE][i] - pawn_square_table_endgame[WHITE][i] * total_material/MAX_MATERIAL;
		score += pawn_square_table[WHITE][i] * total_material/MAX_MATERIAL;
		white_pawns = reset_lsb(white_pawns);
	}

	score += king_square_table_endgame[WHITE][white_king_square] - king_square_table_endgame[WHITE][white_king_square] * total_material/MAX_MATERIAL;
	score += KING_SQUARE_TABLE[white_king_square] * total_material/MAX_MATERIAL;

	// king safety
	uint64_t pawn_mask = (7ULL << (white_king_square + 7)) & ROW_2;

	int king_safety_penalty = 0;

	uint64_t open_files_around_king = open_files & pawn_mask;
	king_safety_penalty += pop_count(open_files_around_king) * UNSAFE_KING_PENALTY;

	// pawns in front of the king
	uint64_t pawn_missing_front_of_king = ~position.p[WHITE][PAWN] & pawn_mask;
	king_safety_penalty += pop_count(pawn_missing_front_of_king) * UNSAFE_KING_PENALTY;

	// no pawns on the two squares in front of the king
	uint64_t pawn_missing_two_squares_front_of_king = ~position.p[WHITE][PAWN] & (pawn_missing_front_of_king << 8);
	king_safety_penalty += pop_count(pawn_missing_two_squares_front_of_king) * UNSAFE_KING_PENALTY;

	// scale the penalty by opponent material
	// we want to exchange pieces when king is unsafe
	king_safety_penalty *= black_piece_material;
	king_safety_penalty /= MAX_MATERIAL;

	score -= king_safety_penalty;

	int white_proximity_bonus = 0;
	uint64_t white_bishops = position.p[WHITE][BISHOP];
	if (pop_count(white_bishops) == 2) {
		score += BISHOP_PAIR_BONUS;
	}
	while (white_bishops) {
		int i = lsb_to_square(white_bishops);
		score += bishop_square_table[WHITE][i];
		score += BISHOP_MOBILITY_BONUS * (pop_count(bishop_attacks(occupied_squares, i) & ~white_squares) - 5);
		white_proximity_bonus += square_proximity[black_king_square][i] * BISHOP_KING_PROXIMITY_BONUS;
		white_bishops = reset_lsb(white_bishops);
	}

	uint64_t white_knights = position.p[WHITE][KNIGHT];
	while (white_knights) {
		int i = lsb_to_square(white_knights);
		score += knight_square_table[WHITE][i];
		white_proximity_bonus += square_proximity[black_king_square][i] * KNIGHT_KING_PROXIMITY_BONUS;
		white_knights = reset_lsb(white_knights);
	}
	uint64_t white_rooks = position.p[WHITE][ROOK];
	uint64_t white_queens = position.p[WHITE][QUEEN];

	uint64_t white_open_file_pieces = open_files & (white_rooks | white_queens);
	score += pop_count(white_open_file_pieces)*OPEN_FILE_BONUS;

	uint64_t white_semi_open_file_pieces = white_semi_open_files & (white_rooks | white_queens);
	score += pop_count(white_semi_open_file_pieces)*SEMI_OPEN_FILE_BONUS;

	while (white_rooks) {
		int i = lsb_to_square(white_rooks);
		score += rook_square_table[WHITE][i];
		score += ROOK_MOBILITY_BONUS * (pop_count(rook_attacks(occupied_squares, i) & ~white_squares) - 5);
		white_proximity_bonus += square_proximity[black_king_square][i] * ROOK_KING_PROXIMITY_BONUS;
		white_rooks = reset_lsb(white_rooks);
	}
	while (white_queens) {
		int queen_square = lsb_to_square(white_queens);
		score += queen_square_table[WHITE][queen_square];
		white_proximity_bonus += square_proximity[black_king_square][queen_square] * QUEEN_KING_PROXIMITY_BONUS;
		white_queens = reset_lsb(white_queens);
	}

	white_proximity_bonus *= white_piece_material;
	white_proximity_bonus /= MAX_MATERIAL;

	score += white_proximity_bonus;

	uint64_t black_pawns = position.p[BLACK][PAWN];

	uint64_t black_passed_pawns = ~white_pawn_blocking_squares & black_pawns;
	score -= pop_count(black_passed_pawns) * PASSED_PAWN_BONUS * (MAX_MATERIAL - total_material) / MAX_MATERIAL;

	uint64_t black_doubled_pawns = black_double_pawn_mask & black_pawns;
	score += pop_count(black_doubled_pawns) * DOUBLED_PAWN_PENALTY;

	uint64_t black_isolated_pawns = black_pawns & ~file_fill(black_pawn_protection_squares);
	score += pop_count(black_isolated_pawns) * ISOLATED_PAWN_PENALTY;

	uint64_t white_dominated_stop_squares = ~south_fill(black_pawn_protection_squares) & white_pawn_protection_squares;
	uint64_t black_backward_pawns =  north_fill(white_dominated_stop_squares) & black_pawns;
	score += pop_count(black_backward_pawns) * BACKWARD_PAWN_PENALTY;

	while (black_pawns) {
		int i = lsb_to_square(black_pawns);
		score -= pawn_square_table_endgame[BLACK][i]  - pawn_square_table_endgame[BLACK][i] * total_material/MAX_MATERIAL;
		score -= pawn_square_table[BLACK][i] * total_material/MAX_MATERIAL;
		black_pawns = reset_lsb(black_pawns);
	}

	score -= king_square_table_endgame[BLACK][black_king_square] - king_square_table_endgame[BLACK][black_king_square] * total_material/MAX_MATERIAL;
	score -= KING_SQUARE_TABLE[black_king_square] * total_material/MAX_MATERIAL;

	int black_king_safety_penalty = 0;

	// king safety
	uint64_t black_pawn_mask = (7ULL << (black_king_square - 9)) & ROW_7;

	uint64_t black_open_files_around_king = open_files & black_pawn_mask;
	black_king_safety_penalty += pop_count(black_open_files_around_king) * UNSAFE_KING_PENALTY;

	// pawns in front of the king
	uint64_t black_pawn_missing_front_of_king = ~position.p[BLACK][PAWN] & black_pawn_mask;
	black_king_safety_penalty += pop_count(black_pawn_missing_front_of_king) * UNSAFE_KING_PENALTY;

	// no pawns on the to squares in front of the king
	uint64_t black_pawn_missing_two_squares_front_of_king = ~position.p[BLACK][PAWN] & (black_pawn_missing_front_of_king >> 8);
	black_king_safety_penalty += pop_count(black_pawn_missing_two_squares_front_of_king) * UNSAFE_KING_PENALTY;

	// scale the penalty by opponent material
	// we want to exchange pieces when king is unsafe
	black_king_safety_penalty *= white_piece_material;
	black_king_safety_penalty /= MAX_MATERIAL;

	score += black_king_safety_penalty;

	int black_proximity_bonus = 0;
	uint64_t black_bishops = position.p[BLACK][BISHOP];
	if (pop_count(black_bishops) == 2) {
		score -= BISHOP_PAIR_BONUS;
	}
	while (black_bishops) {
		int i = lsb_to_square(black_bishops);
		score -= bishop_square_table[BLACK][i];
		score -= BISHOP_MOBILITY_BONUS * (pop_count(bishop_attacks(occupied_squares, i) & ~black_squares) - 5);
		black_proximity_bonus += square_proximity[white_king_square][i] * BISHOP_KING_PROXIMITY_BONUS;
		black_bishops = reset_lsb(black_bishops);
	}

	uint64_t black_knights = position.p[BLACK][KNIGHT];
	while (black_knights) {
		int i = lsb_to_square(black_knights);
		score -= knight_square_table[BLACK][i];
		black_proximity_bonus += square_proximity[white_king_square][i] * KNIGHT_KING_PROXIMITY_BONUS;
		black_knights = reset_lsb(black_knights);
	}
	uint64_t black_rooks = position.p[BLACK][ROOK];
	uint64_t black_queens = position.p[BLACK][QUEEN];

	uint64_t black_open_file_pieces = open_files & (black_rooks | black_queens);
	score -= pop_count(black_open_file_pieces)*OPEN_FILE_BONUS;

	uint64_t black_semi_open_file_pieces = black_semi_open_files & (black_rooks | black_queens);
	score -= pop_count(black_semi_open_file_pieces)*SEMI_OPEN_FILE_BONUS;

	while (black_rooks) {
		int i = lsb_to_square(black_rooks);
		score -= rook_square_table[BLACK][i];
		score -= ROOK_MOBILITY_BONUS * (pop_count(rook_attacks(occupied_squares, i) & ~black_squares) - 5);
		black_proximity_bonus += square_proximity[white_king_square][i] * ROOK_KING_PROXIMITY_BONUS;
		black_rooks = reset_lsb(black_rooks);
	}
	while (black_queens) {
		int queen_square = lsb_to_square(black_queens);
		score -= queen_square_table[BLACK][queen_square];
		black_proximity_bonus += square_proximity[white_king_square][queen_square] * QUEEN_KING_PROXIMITY_BONUS;
		black_queens = reset_lsb(black_queens);
	}

	black_proximity_bonus *= black_piece_material;
	black_proximity_bonus /= MAX_MATERIAL;

	score -= black_proximity_bonus;

	return score;
}

/**
 * S(x) is a s-shaped curve from 0 - 1 where
 *
 * S(0) = 0.5
 * S(high) ~ 0.9
 * S(1-high) ~ 0.1
 *
 */
double sigmoid(double x, double high) {
	return 1 / (1 + pow(10, -x / (high/2)));
}

/*
 * The value on a square is the sum of the base value and a bonus for being near the center and opponent's back row.
 *
 * The bonuses are calculated using the S-shaped sigmoid function.
 *
 */
int calculate_square_value(int base_piece_value, int center_bonus, int center_s_max, int opponent_back_row_bonus, int back_row_s_max, int side, int square) {
	double square_value = base_piece_value;

	int row = side == WHITE ? square / 8 : 7 - (square / 8);
	int col = side == WHITE ? square % 8 : square % 8;

	double AVG_CENTER_DISTANCE = 3.5;

	// center_proximity is a value between -1.5 to 1.5
	double center_proximity = 2 - std::max(fabs(row - AVG_CENTER_DISTANCE), fabs(col - AVG_CENTER_DISTANCE));

	square_value += center_bonus * sigmoid(center_proximity, center_s_max);

	double AVG_OPPONENT_BACK_ROW_DISTANCE = 3.5;

	// row8_poximity is a value between -3.5 to 3.5
	double opponent_back_row_proximity = row - AVG_OPPONENT_BACK_ROW_DISTANCE;

	square_value += opponent_back_row_bonus * sigmoid(opponent_back_row_proximity, back_row_s_max);

	return (int)(square_value + 0.5);
}


void generate_piece_square_table(int (&psqt)[2][64], int piece_value, int center_bonus, int center_s_max, int opponent_back_row_bonus, int backrow_s_max) {
	for (int i = 0; i < 64; i++) {
		psqt[WHITE][i] = calculate_square_value(piece_value, center_bonus, center_s_max, opponent_back_row_bonus, backrow_s_max, WHITE, i);
		psqt[BLACK][i] = calculate_square_value(piece_value, center_bonus, center_s_max, opponent_back_row_bonus, backrow_s_max, BLACK, i);
	}
}

void init_eval() {
	for (int i = 0; i < 64; ++i) {
		for (int j = 0; j < 64; ++j) {
			int row_i = i / 8;
			int file_i = i % 8;
			int row_j = j / 8;
			int file_j = j % 8;
			square_proximity[i][j] = 7 - std::max(std::abs(file_i - file_j), std::abs(row_i - row_j));
		}
		rook_square_table[WHITE][i] = ROOK_SQUARE_TABLE[i];
		rook_square_table[BLACK][i] = ROOK_SQUARE_TABLE[63 - i];
	}

	generate_piece_square_table(knight_square_table, KNIGHT_PSQT_BASE_VALUE, KNIGHT_CENTER_BONUS, KNIGHT_CENTER_S_MAX, KNIGHT_OPPONENT_BACK_ROW_BONUS, KNIGHT_OPPONENT_BACK_ROW_S_MAX);
	generate_piece_square_table(bishop_square_table, BISHOP_PSQT_BASE_VALUE, BISHOP_CENTER_BONUS, BISHOP_CENTER_S_MAX, BISHOP_OPPONENT_BACK_ROW_BONUS, BISHOP_OPPONENT_BACK_ROW_S_MAX);
	generate_piece_square_table(king_square_table_endgame, KING_PSQT_BASE_VALUE_EG, KING_CENTER_BONUS_EG, KING_CENTER_S_MAX_EG, KING_OPPONENT_BACK_ROW_BONUS_EG, KING_OPPONENT_BACK_ROW_S_MAX_EG);
	generate_piece_square_table(pawn_square_table, PAWN_PSQT_BASE_VALUE_MG, PAWN_CENTER_BONUS_MG, PAWN_CENTER_S_MAX_MG, PAWN_OPPONENT_BACK_ROW_BONUS_MG, PAWN_OPPONENT_BACK_ROW_S_MAX_MG);
	generate_piece_square_table(pawn_square_table_endgame, PAWN_PSQT_BASE_VALUE_EG, PAWN_CENTER_BONUS_EG, PAWN_CENTER_S_MAX_EG, PAWN_OPPONENT_BACK_ROW_BONUS_EG, PAWN_OPPONENT_BACK_ROW_S_MAX_EG);
	generate_piece_square_table(queen_square_table, QUEEN_PSQT_BASE_VALUE, QUEEN_CENTER_BONUS, QUEEN_CENTER_S_MAX, QUEEN_OPPONENT_BACK_ROW_BONUS, QUEEN_OPPONENT_BACK_ROW_S_MAX);

}
