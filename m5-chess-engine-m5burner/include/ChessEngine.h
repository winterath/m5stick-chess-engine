#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

struct ChessMove {
  int from = -1;
  int to = -1;
  char promotion = 0;
  bool castle = false;
  bool enPassant = false;
};

struct ChessSearchResult {
  ChessMove bestMove;
  int score = 0;
  uint32_t nodes = 0;
  int depth = 0;
  bool hasMove = false;
  bool checkmate = false;
  bool stalemate = false;
};

class ChessEngine {
 public:
  static constexpr const char* START_FEN =
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

  ChessEngine() { setFromFen(START_FEN); }

  bool setFromFen(const std::string& fen, std::string* error = nullptr) {
    for (char& square : board_) {
      square = '.';
    }

    std::vector<std::string> parts = split(fen, ' ');
    if (parts.size() < 4) {
      setError(error, "FEN needs placement, side, castling, and en-passant fields");
      return false;
    }

    int rank = 7;
    int file = 0;
    int ranksSeen = 1;
    for (char ch : parts[0]) {
      if (ch == '/') {
        if (file != 8 || rank == 0) {
          setError(error, "Bad FEN board layout");
          return false;
        }
        --rank;
        file = 0;
        ++ranksSeen;
        continue;
      }

      if (std::isdigit(static_cast<unsigned char>(ch))) {
        int empty = ch - '0';
        if (empty < 1 || empty > 8 || file + empty > 8) {
          setError(error, "Bad empty-square count in FEN");
          return false;
        }
        file += empty;
        continue;
      }

      if (!isFenPiece(ch) || file >= 8 || rank < 0) {
        setError(error, "Bad piece in FEN");
        return false;
      }
      board_[rank * 8 + file] = ch;
      ++file;
    }

    if (rank != 0 || file != 8 || ranksSeen != 8) {
      setError(error, "FEN board must contain eight complete ranks");
      return false;
    }

    if (parts[1] == "w") {
      whiteToMove_ = true;
    } else if (parts[1] == "b") {
      whiteToMove_ = false;
    } else {
      setError(error, "FEN side-to-move must be w or b");
      return false;
    }

    castlingRights_ = 0;
    if (parts[2] != "-") {
      for (char ch : parts[2]) {
        switch (ch) {
          case 'K': castlingRights_ |= 1; break;
          case 'Q': castlingRights_ |= 2; break;
          case 'k': castlingRights_ |= 4; break;
          case 'q': castlingRights_ |= 8; break;
          default:
            setError(error, "Bad castling rights in FEN");
            return false;
        }
      }
    }

    if (parts[3] == "-") {
      epSquare_ = -1;
    } else {
      epSquare_ = squareFromName(parts[3]);
      if (epSquare_ < 0) {
        setError(error, "Bad en-passant square in FEN");
        return false;
      }
    }

    halfmoveClock_ = parts.size() > 4 ? std::max(0, std::atoi(parts[4].c_str())) : 0;
    fullmoveNumber_ = parts.size() > 5 ? std::max(1, std::atoi(parts[5].c_str())) : 1;
    return true;
  }

  std::string toFen() const {
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
      int empty = 0;
      for (int file = 0; file < 8; ++file) {
        char piece = board_[rank * 8 + file];
        if (piece == '.') {
          ++empty;
        } else {
          if (empty) {
            fen += static_cast<char>('0' + empty);
            empty = 0;
          }
          fen += piece;
        }
      }
      if (empty) {
        fen += static_cast<char>('0' + empty);
      }
      if (rank > 0) {
        fen += '/';
      }
    }

    fen += whiteToMove_ ? " w " : " b ";
    std::string castling;
    if (castlingRights_ & 1) castling += 'K';
    if (castlingRights_ & 2) castling += 'Q';
    if (castlingRights_ & 4) castling += 'k';
    if (castlingRights_ & 8) castling += 'q';
    fen += castling.empty() ? "-" : castling;
    fen += ' ';
    fen += epSquare_ >= 0 ? squareName(epSquare_) : "-";
    fen += ' ';
    fen += std::to_string(halfmoveClock_);
    fen += ' ';
    fen += std::to_string(fullmoveNumber_);
    return fen;
  }

  bool whiteToMove() const { return whiteToMove_; }

  bool inCheck() const { return kingInCheck(whiteToMove_); }

  std::vector<ChessMove> legalMoves() const {
    std::vector<ChessMove> moves;
    std::vector<ChessMove> pseudo = pseudoLegalMoves();
    moves.reserve(pseudo.size());

    for (const ChessMove& move : pseudo) {
      ChessEngine next = *this;
      next.applyUnchecked(move);
      bool sideThatMoved = !next.whiteToMove_;
      if (!next.kingInCheck(sideThatMoved)) {
        moves.push_back(move);
      }
    }
    return moves;
  }

  bool makeMoveUci(const std::string& text, std::string* error = nullptr) {
    if (text.size() < 4) {
      setError(error, "Move must look like e2e4 or a7a8q");
      return false;
    }

    int from = squareFromName(text.substr(0, 2));
    int to = squareFromName(text.substr(2, 2));
    char promotion = text.size() >= 5 ? normalizePromotion(text[4]) : 0;
    if (from < 0 || to < 0 || (text.size() >= 5 && promotion == 0)) {
      setError(error, "Bad move coordinate");
      return false;
    }

    for (const ChessMove& move : legalMoves()) {
      if (move.from != from || move.to != to) {
        continue;
      }
      if (move.promotion != 0) {
        if (promotion != 0 && promotion == move.promotion) {
          applyUnchecked(move);
          return true;
        }
        if (promotion == 0 && move.promotion == 'q') {
          applyUnchecked(move);
          return true;
        }
      } else if (promotion == 0) {
        applyUnchecked(move);
        return true;
      }
    }

    setError(error, "Illegal move in the current position");
    return false;
  }

  ChessSearchResult search(int depth) const {
    ChessSearchResult result;
    result.depth = std::max(1, depth);
    std::vector<ChessMove> moves = legalMoves();
    if (moves.empty()) {
      result.checkmate = kingInCheck(whiteToMove_);
      result.stalemate = !result.checkmate;
      result.score = result.checkmate ? -kMateScore : 0;
      return result;
    }

    orderMoves(moves);

    int bestScore = -kInfinity;
    int alpha = -kInfinity;
    int beta = kInfinity;
    for (const ChessMove& move : moves) {
      ChessEngine next = *this;
      next.applyUnchecked(move);
      int score = -next.negamax(result.depth - 1, -beta, -alpha, result.nodes, 1);
      if (!result.hasMove || score > bestScore) {
        bestScore = score;
        result.bestMove = move;
        result.hasMove = true;
      }
      alpha = std::max(alpha, score);
    }
    result.score = bestScore;
    return result;
  }

  std::string moveToUci(const ChessMove& move) const {
    if (move.from < 0 || move.to < 0) {
      return "(none)";
    }
    std::string text = squareName(move.from) + squareName(move.to);
    if (move.promotion) {
      text += move.promotion;
    }
    return text;
  }

 private:
  static constexpr int kInfinity = 30000;
  static constexpr int kMateScore = 29000;

  char board_[64] = {};
  bool whiteToMove_ = true;
  uint8_t castlingRights_ = 0;
  int epSquare_ = -1;
  int halfmoveClock_ = 0;
  int fullmoveNumber_ = 1;

  static void setError(std::string* error, const char* message) {
    if (error) {
      *error = message;
    }
  }

  static std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : text) {
      if (ch == delimiter) {
        if (!current.empty()) {
          parts.push_back(current);
          current.clear();
        }
      } else {
        current += ch;
      }
    }
    if (!current.empty()) {
      parts.push_back(current);
    }
    return parts;
  }

  static bool isFenPiece(char ch) {
    switch (ch) {
      case 'P': case 'N': case 'B': case 'R': case 'Q': case 'K':
      case 'p': case 'n': case 'b': case 'r': case 'q': case 'k':
        return true;
      default:
        return false;
    }
  }

  static bool isWhitePiece(char ch) { return ch >= 'A' && ch <= 'Z'; }

  static bool isBlackPiece(char ch) { return ch >= 'a' && ch <= 'z'; }

  static bool isFriendly(char piece, bool white) {
    return white ? isWhitePiece(piece) : isBlackPiece(piece);
  }

  static bool isEnemy(char piece, bool white) {
    return piece != '.' && (white ? isBlackPiece(piece) : isWhitePiece(piece));
  }

  static int fileOf(int square) { return square & 7; }

  static int rankOf(int square) { return square >> 3; }

  static bool inside(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
  }

  static int square(int file, int rank) { return rank * 8 + file; }

  static std::string squareName(int squareIndex) {
    std::string name;
    name += static_cast<char>('a' + fileOf(squareIndex));
    name += static_cast<char>('1' + rankOf(squareIndex));
    return name;
  }

  static int squareFromName(const std::string& name) {
    if (name.size() != 2) {
      return -1;
    }
    char fileChar = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    char rankChar = name[1];
    if (fileChar < 'a' || fileChar > 'h' || rankChar < '1' || rankChar > '8') {
      return -1;
    }
    return square(fileChar - 'a', rankChar - '1');
  }

  static char normalizePromotion(char ch) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return (ch == 'q' || ch == 'r' || ch == 'b' || ch == 'n') ? ch : 0;
  }

  static int pieceValue(char piece) {
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(piece)))) {
      case 'p': return 100;
      case 'n': return 320;
      case 'b': return 330;
      case 'r': return 500;
      case 'q': return 900;
      default: return 0;
    }
  }

  static int centerBonus(int file, int rank) {
    int df = std::min(std::abs(file - 3), std::abs(file - 4));
    int dr = std::min(std::abs(rank - 3), std::abs(rank - 4));
    return 18 - (df + dr) * 5;
  }

  int evaluateWhite() const {
    int score = 0;
    for (int squareIndex = 0; squareIndex < 64; ++squareIndex) {
      char piece = board_[squareIndex];
      if (piece == '.') {
        continue;
      }

      bool white = isWhitePiece(piece);
      int file = fileOf(squareIndex);
      int rank = rankOf(squareIndex);
      int rankFromOwner = white ? rank : 7 - rank;
      char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(piece)));
      int value = pieceValue(piece);

      if (lower == 'p') {
        value += rankFromOwner * 7;
      } else if (lower == 'n' || lower == 'b') {
        value += centerBonus(file, rank);
      } else if (lower == 'q') {
        value += centerBonus(file, rank) / 2;
      }

      score += white ? value : -value;
    }
    return score;
  }

  int evaluateForSide() const {
    int whiteScore = evaluateWhite();
    return whiteToMove_ ? whiteScore : -whiteScore;
  }

  void addPawnMove(std::vector<ChessMove>& moves, int from, int to, bool enPassant = false) const {
    int promotionRank = whiteToMove_ ? 7 : 0;
    if (rankOf(to) == promotionRank) {
      moves.push_back(ChessMove{from, to, 'q', false, enPassant});
      moves.push_back(ChessMove{from, to, 'r', false, enPassant});
      moves.push_back(ChessMove{from, to, 'b', false, enPassant});
      moves.push_back(ChessMove{from, to, 'n', false, enPassant});
    } else {
      moves.push_back(ChessMove{from, to, 0, false, enPassant});
    }
  }

  void addSlidingMoves(std::vector<ChessMove>& moves, int from, const int directions[][2], int count) const {
    bool white = whiteToMove_;
    for (int i = 0; i < count; ++i) {
      int file = fileOf(from) + directions[i][0];
      int rank = rankOf(from) + directions[i][1];
      while (inside(file, rank)) {
        int to = square(file, rank);
        char target = board_[to];
        if (target == '.') {
          moves.push_back(ChessMove{from, to});
        } else {
          if (isEnemy(target, white)) {
            moves.push_back(ChessMove{from, to});
          }
          break;
        }
        file += directions[i][0];
        rank += directions[i][1];
      }
    }
  }

  std::vector<ChessMove> pseudoLegalMoves() const {
    std::vector<ChessMove> moves;
    moves.reserve(96);

    static constexpr int knightOffsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    static constexpr int bishopDirs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    static constexpr int rookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    static constexpr int queenDirs[8][2] = {
        {1, 1}, {-1, 1}, {1, -1}, {-1, -1},
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    for (int from = 0; from < 64; ++from) {
      char piece = board_[from];
      if (piece == '.' || !isFriendly(piece, whiteToMove_)) {
        continue;
      }

      int file = fileOf(from);
      int rank = rankOf(from);
      char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(piece)));

      if (lower == 'p') {
        int direction = whiteToMove_ ? 1 : -1;
        int startRank = whiteToMove_ ? 1 : 6;
        int oneRank = rank + direction;
        if (inside(file, oneRank)) {
          int one = square(file, oneRank);
          if (board_[one] == '.') {
            addPawnMove(moves, from, one);
            int twoRank = rank + direction * 2;
            if (rank == startRank && inside(file, twoRank)) {
              int two = square(file, twoRank);
              if (board_[two] == '.') {
                moves.push_back(ChessMove{from, two});
              }
            }
          }
        }

        for (int df : {-1, 1}) {
          int captureFile = file + df;
          int captureRank = rank + direction;
          if (!inside(captureFile, captureRank)) {
            continue;
          }
          int to = square(captureFile, captureRank);
          if (isEnemy(board_[to], whiteToMove_)) {
            addPawnMove(moves, from, to);
          } else if (to == epSquare_) {
            addPawnMove(moves, from, to, true);
          }
        }
      } else if (lower == 'n') {
        for (const auto& offset : knightOffsets) {
          int toFile = file + offset[0];
          int toRank = rank + offset[1];
          if (!inside(toFile, toRank)) {
            continue;
          }
          int to = square(toFile, toRank);
          if (!isFriendly(board_[to], whiteToMove_)) {
            moves.push_back(ChessMove{from, to});
          }
        }
      } else if (lower == 'b') {
        addSlidingMoves(moves, from, bishopDirs, 4);
      } else if (lower == 'r') {
        addSlidingMoves(moves, from, rookDirs, 4);
      } else if (lower == 'q') {
        addSlidingMoves(moves, from, queenDirs, 8);
      } else if (lower == 'k') {
        for (int df = -1; df <= 1; ++df) {
          for (int dr = -1; dr <= 1; ++dr) {
            if (df == 0 && dr == 0) {
              continue;
            }
            int toFile = file + df;
            int toRank = rank + dr;
            if (!inside(toFile, toRank)) {
              continue;
            }
            int to = square(toFile, toRank);
            if (!isFriendly(board_[to], whiteToMove_)) {
              moves.push_back(ChessMove{from, to});
            }
          }
        }
        addCastlingMoves(moves);
      }
    }
    return moves;
  }

  void addCastlingMoves(std::vector<ChessMove>& moves) const {
    if (whiteToMove_) {
      if ((castlingRights_ & 1) && board_[4] == 'K' && board_[7] == 'R' &&
          board_[5] == '.' && board_[6] == '.' &&
          !isSquareAttacked(4, false) && !isSquareAttacked(5, false) && !isSquareAttacked(6, false)) {
        moves.push_back(ChessMove{4, 6, 0, true, false});
      }
      if ((castlingRights_ & 2) && board_[4] == 'K' && board_[0] == 'R' &&
          board_[3] == '.' && board_[2] == '.' && board_[1] == '.' &&
          !isSquareAttacked(4, false) && !isSquareAttacked(3, false) && !isSquareAttacked(2, false)) {
        moves.push_back(ChessMove{4, 2, 0, true, false});
      }
    } else {
      if ((castlingRights_ & 4) && board_[60] == 'k' && board_[63] == 'r' &&
          board_[61] == '.' && board_[62] == '.' &&
          !isSquareAttacked(60, true) && !isSquareAttacked(61, true) && !isSquareAttacked(62, true)) {
        moves.push_back(ChessMove{60, 62, 0, true, false});
      }
      if ((castlingRights_ & 8) && board_[60] == 'k' && board_[56] == 'r' &&
          board_[59] == '.' && board_[58] == '.' && board_[57] == '.' &&
          !isSquareAttacked(60, true) && !isSquareAttacked(59, true) && !isSquareAttacked(58, true)) {
        moves.push_back(ChessMove{60, 58, 0, true, false});
      }
    }
  }

  bool isSquareAttacked(int target, bool byWhite) const {
    int file = fileOf(target);
    int rank = rankOf(target);

    if (byWhite) {
      if (file < 7 && rank > 0 && board_[target - 7] == 'P') return true;
      if (file > 0 && rank > 0 && board_[target - 9] == 'P') return true;
    } else {
      if (file < 7 && rank < 7 && board_[target + 9] == 'p') return true;
      if (file > 0 && rank < 7 && board_[target + 7] == 'p') return true;
    }

    static constexpr int knightOffsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    char knight = byWhite ? 'N' : 'n';
    for (const auto& offset : knightOffsets) {
      int sourceFile = file + offset[0];
      int sourceRank = rank + offset[1];
      if (inside(sourceFile, sourceRank) && board_[square(sourceFile, sourceRank)] == knight) {
        return true;
      }
    }

    if (attackedBySlider(target, byWhite, true)) return true;
    if (attackedBySlider(target, byWhite, false)) return true;

    char king = byWhite ? 'K' : 'k';
    for (int df = -1; df <= 1; ++df) {
      for (int dr = -1; dr <= 1; ++dr) {
        if (df == 0 && dr == 0) continue;
        int sourceFile = file + df;
        int sourceRank = rank + dr;
        if (inside(sourceFile, sourceRank) && board_[square(sourceFile, sourceRank)] == king) {
          return true;
        }
      }
    }
    return false;
  }

  bool attackedBySlider(int target, bool byWhite, bool diagonal) const {
    static constexpr int bishopDirs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    static constexpr int rookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    const int (*dirs)[2] = diagonal ? bishopDirs : rookDirs;
    char first = byWhite ? (diagonal ? 'B' : 'R') : (diagonal ? 'b' : 'r');
    char queen = byWhite ? 'Q' : 'q';

    for (int i = 0; i < 4; ++i) {
      int file = fileOf(target) + dirs[i][0];
      int rank = rankOf(target) + dirs[i][1];
      while (inside(file, rank)) {
        char piece = board_[square(file, rank)];
        if (piece != '.') {
          if (piece == first || piece == queen) {
            return true;
          }
          break;
        }
        file += dirs[i][0];
        rank += dirs[i][1];
      }
    }
    return false;
  }

  bool kingInCheck(bool whiteKing) const {
    char king = whiteKing ? 'K' : 'k';
    for (int i = 0; i < 64; ++i) {
      if (board_[i] == king) {
        return isSquareAttacked(i, !whiteKing);
      }
    }
    return true;
  }

  void applyUnchecked(const ChessMove& move) {
    char moving = board_[move.from];
    char target = board_[move.to];
    bool white = isWhitePiece(moving);
    bool pawnMove = std::tolower(static_cast<unsigned char>(moving)) == 'p';
    bool capture = target != '.' || move.enPassant;

    board_[move.from] = '.';
    if (move.enPassant) {
      int capturedPawn = move.to + (white ? -8 : 8);
      board_[capturedPawn] = '.';
    }

    char placed = moving;
    if (move.promotion) {
      placed = white ? static_cast<char>(std::toupper(move.promotion)) : move.promotion;
    }
    board_[move.to] = placed;

    if (move.castle) {
      if (move.to == 6) {
        board_[5] = board_[7];
        board_[7] = '.';
      } else if (move.to == 2) {
        board_[3] = board_[0];
        board_[0] = '.';
      } else if (move.to == 62) {
        board_[61] = board_[63];
        board_[63] = '.';
      } else if (move.to == 58) {
        board_[59] = board_[56];
        board_[56] = '.';
      }
    }

    updateCastlingRights(move.from, move.to, moving, target);

    epSquare_ = -1;
    if (pawnMove && std::abs(move.to - move.from) == 16) {
      epSquare_ = (move.from + move.to) / 2;
    }

    halfmoveClock_ = (pawnMove || capture) ? 0 : halfmoveClock_ + 1;
    if (!whiteToMove_) {
      ++fullmoveNumber_;
    }
    whiteToMove_ = !whiteToMove_;
  }

  void updateCastlingRights(int from, int to, char moving, char captured) {
    if (moving == 'K') castlingRights_ &= static_cast<uint8_t>(~3);
    if (moving == 'k') castlingRights_ &= static_cast<uint8_t>(~12);
    if (moving == 'R' && from == 0) castlingRights_ &= static_cast<uint8_t>(~2);
    if (moving == 'R' && from == 7) castlingRights_ &= static_cast<uint8_t>(~1);
    if (moving == 'r' && from == 56) castlingRights_ &= static_cast<uint8_t>(~8);
    if (moving == 'r' && from == 63) castlingRights_ &= static_cast<uint8_t>(~4);
    if (captured == 'R' && to == 0) castlingRights_ &= static_cast<uint8_t>(~2);
    if (captured == 'R' && to == 7) castlingRights_ &= static_cast<uint8_t>(~1);
    if (captured == 'r' && to == 56) castlingRights_ &= static_cast<uint8_t>(~8);
    if (captured == 'r' && to == 63) castlingRights_ &= static_cast<uint8_t>(~4);
  }

  void orderMoves(std::vector<ChessMove>& moves) const {
    std::sort(moves.begin(), moves.end(), [this](const ChessMove& a, const ChessMove& b) {
      return moveScore(a) > moveScore(b);
    });
  }

  int moveScore(const ChessMove& move) const {
    int score = 0;
    char moving = board_[move.from];
    char target = board_[move.to];
    if (target != '.') {
      score += 10000 + pieceValue(target) - pieceValue(moving) / 10;
    }
    if (move.enPassant) {
      score += 10100;
    }
    if (move.promotion) {
      score += 9000 + pieceValue(move.promotion);
    }
    if (move.castle) {
      score += 40;
    }
    return score;
  }

  int negamax(int depth, int alpha, int beta, uint32_t& nodes, int ply) const {
    ++nodes;
    if (depth <= 0) {
      return evaluateForSide();
    }

    std::vector<ChessMove> moves = legalMoves();
    if (moves.empty()) {
      return kingInCheck(whiteToMove_) ? -kMateScore + ply : 0;
    }
    orderMoves(moves);

    int best = -kInfinity;
    for (const ChessMove& move : moves) {
      ChessEngine next = *this;
      next.applyUnchecked(move);
      int score = -next.negamax(depth - 1, -beta, -alpha, nodes, ply + 1);
      best = std::max(best, score);
      alpha = std::max(alpha, score);
      if (alpha >= beta) {
        break;
      }
    }
    return best;
  }
};
