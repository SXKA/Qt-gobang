#include "gobang.h"

using namespace gobang;

const std::unordered_map<std::string, score> Gobang::shapeScoreMap = {
    {"001000", one}, {"000100", one},
    {"010100", two}, {"001010", two}, {"001100", two},
    {"011100", three}, {"001110", three}, {"010110", three}, {"011010", three},
    {"11110", four}, {"01111", four}, {"10111", four}, {"11011", four}, {"11101", four},
    {"011110", livingFour},
    {"11111", five}
};

Gobang::Gobang()
{
    vacancies = QSet<QPoint>();
    record = QStack<QPoint>();
    bestPoint = QPoint();
    zobrist = zobrist::Zobrist();
    trie = aho_corasick::trie();
    board = std::array<std::array<stone, 15>, 15> {};
    blackScores = std::array<int, 72> {};
    whiteScores = std::array<int, 72> {};
    blackTotalScore = 0;
    whiteTotalScore = 0;

    for (int i = 0; i < 15; ++i) {
        for (int j = 0; j < 15; ++j) {
            vacancies.insert(QPoint(i, j));
            board[i][j] = empty;
        }
    }

    for (const auto &shapeScore : shapeScoreMap) {
        trie.insert(shapeScore.first);
    }
}

bool Gobang::isLegal(const QPoint &point)
{
    if (point.x() >= 0 && point.x() < 15 && point.y() >= 0 && point.y() < 15) {
        return true;
    }

    return false;
}

void Gobang::back(const int &step)
{
    for (int i = 0; i < step; ++i) {
        QPoint point = record.top();

        record.pop();
        vacancies.insert(point);
        zobrist.translate(point, checkStone(point));
        board[point.x()][point.y()] = empty;

        updateScore(point);
    }
}

void Gobang::play(const QPoint &point, const stone &stone)
{
    vacancies.remove(point);
    record.push(point);
    zobrist.translate(point, stone);
    board[point.x()][point.y()] = stone;

    updateScore(point);
}

bool Gobang::gameOver(const QPoint &point, const stone &stone) const
{
    QString winner = stone == black ? "Black" : "White";

    winner.append(" win!");

    switch (gameState(point, stone)) {
    case draw:
        QMessageBox::information(nullptr, "Result", "Draw!", QMessageBox::Ok, QMessageBox::NoButton);

        return true;
    case undecided:
        break;
    case win:
        QMessageBox::information(nullptr, "Result", winner, QMessageBox::Ok, QMessageBox::NoButton);

        return true;
    }

    return false;
}

bool Gobang::isInitial(const bool &type, const stone &stone) const
{
    if(!type) {
        if(record.empty()) {
            return true;
        }

        if(record.size() == 1) {
	        return checkStone(record.top()) != stone;
        }
    }

    return record.empty();
}

stone Gobang::checkStone(const QPoint &point) const
{
    return board[point.x()][point.y()];
}

state Gobang::gameState(const QPoint &point, const stone &stone) const
{
    const std::array<int, 2> d = {-1, 1};
    const std::array<int, 4> dx = {1, 0, 1, 1};
    const std::array<int, 4> dy = {0, 1, 1, -1};

    for (int i = 0; i < 4; ++i) {
        int count = 1;

        for (int j = 0; j < 2; ++j) {
            int x = point.x() + d[j] * dx[i];
            int y = point.y() + d[j] * dy[i];

            while (isLegal(QPoint(x, y)) && checkStone(QPoint(x, y)) == stone) {
                ++count;

                x += d[j] * dx[i];
                y += d[j] * dy[i];
            }
        }

        if (count >= 5) {
            return win;
        }
    }

    for (const auto &row : board) {
        if (std::any_of(row.cbegin(), row.cend(), [](const auto & x) {
        return x == empty;
    })) {
            return undecided;
        }
    }

    return draw;
}

QPoint Gobang::ai(const stone &stone)
{
    const auto score = alphaBetaPrune(stone, maxDepth);

    qDebug() << "Score: " << score;

    return bestPoint;
}

QPoint Gobang::lastStone() const
{
    return record.empty() ? QPoint() : record.top();
}

void Gobang::updateScore(const QPoint &point)
{
    std::array<std::string, 4> blackLines;
    std::array<std::string, 4> whiteLines;
    auto insertToLine = [this, &blackLines, &whiteLines](const auto & i, const auto & p) {
        switch (checkStone(p)) {
        case empty:
            blackLines[i].push_back('0');
            whiteLines[i].push_back('0');

            break;
        case black:
            blackLines[i].push_back('1');
            whiteLines[i].push_back(' ');

            break;
        case white:
            blackLines[i].push_back(' ');
            whiteLines[i].push_back('1');

            break;
        }
    };
    const auto x = point.x();
    const auto y = point.y();

    for (int i = 0; i < 15; ++i) {
        insertToLine(0, QPoint(i, y));
        insertToLine(1, QPoint(x, i));
    }

    auto base = std::min(x, y);

    for (int i = x - base, j = y - base; i < 15 && j < 15; ++i, ++j) {
        insertToLine(2, QPoint(i, j));
    }

    base = std::min(x, 14 - y);

    for (int i = x - base, j = y + base; i < 15 && j >= 0; ++i, --j) {
        insertToLine(3, QPoint(i, j));
    }

    std::array<int, 4> blackLineScores{};
    std::array<int, 4> whiteLineScores{};

    for (int i = 0; i < 4; ++i) {
        auto shapes = trie.parse_text(blackLines[i]);

        for (const auto &shape : shapes) {
            blackLineScores[i] += static_cast<int>(shapeScoreMap.at(shape.get_keyword()));
        }

        shapes = trie.parse_text(whiteLines[i]);

        for (const auto &shape : shapes) {
            whiteLineScores[i] += static_cast<int>(shapeScoreMap.at(shape.get_keyword()));
        }
    }

    auto offsetIndex = x + 15;
    auto update = [this](const auto & index, const auto & blackLineScore, const auto & whiteLineScore) {
        blackTotalScore -= blackScores[index];
        whiteTotalScore -= whiteScores[index];
        blackScores[index] = blackLineScore;
        whiteScores[index] = whiteLineScore;
        blackTotalScore += blackScores[index];
        whiteTotalScore += whiteScores[index];
    };

    update(y, blackLineScores[0], whiteLineScores[0]);
    update(offsetIndex, blackLineScores[1], whiteLineScores[1]);

    offsetIndex = x - y + 40;

    if (std::abs(x - y) <= 10) {
        update(offsetIndex, blackLineScores[2], whiteLineScores[2]);
    }

    offsetIndex = x + y + 47;

    if (x + y >= 4 && x + y <= 24) {
        update(offsetIndex, blackLineScores[3], whiteLineScores[3]);
    }
}

bool Gobang::isIsolated(const QPoint &point) const
{
    for (int i = -1; i <= 1; ++i) {
        for (int j = -1; j <= 1; ++j) {
            const QPoint neighborhood = point + QPoint(i, j);

            if (isLegal(neighborhood) && checkStone(neighborhood) != empty) {
                return false;
            }
        }
    }

    return true;
}

int Gobang::alphaBetaPrune(const stone &stone, const int &depth, int alpha, const int &beta)
{
    if (depth != maxDepth && zobrist.contains(zobrist.hash(), depth)) {
        const auto entry = zobrist.at(zobrist.hash());

        switch (entry.type) {
        case zobrist::type::empty:
            qDebug() << "Hash error!";

            break;
        case zobrist::exact:
            return entry.score;
        case zobrist::lowerBound:
            if (entry.score >= beta) {
                return beta;
            }

            break;
        case zobrist::upperBound:
            if (entry.score <= alpha) {
                return alpha;
            }

            break;
        }
    }

    const auto firstScore = evaluate(stone);
    const auto secondScore = evaluate(static_cast<const gobang::stone>(!stone));

    if (firstScore >= five) {
        return maxScore - 1000 - (maxDepth - depth);
    }

    if (secondScore >= five) {
        return minScore + 1000 + (maxDepth - depth);
    }

    if (!depth) {
        zobrist.insert(zobrist.hash(), zobrist::exact, depth, firstScore - secondScore);

        return firstScore - secondScore;
    }

    const auto scoreGreater = [](const auto & a, const auto & b) {
        return a.second != b.second
               ? a.second > b.second
               : a.first.x() != b.first.x()
               ? a.first.x() < b.first.x()
               : a.first.y() < b.first.y();
    };

    std::set<std::pair<QPoint, int>, decltype(scoreGreater)> candidates(scoreGreater);

    for (const auto &vacancy : vacancies) {
        if (isIsolated(vacancy)) {
            continue;
        }

        candidates.emplace(vacancy, calculateScore(vacancy));
    }

    int limit = 8 - (maxDepth - depth);
    auto valueType = zobrist::upperBound;

    for (const auto& [candidate, score] : candidates) {
        play(candidate, stone);

        const auto vacancyScore = -alphaBetaPrune(static_cast<const gobang::stone>(!stone), depth - 1,
                                                  -beta, -alpha);

        back(1);

        if (vacancyScore >= beta) {
            zobrist.insert(zobrist.hash(), zobrist::lowerBound, depth, vacancyScore);

            return beta;
        }

        if (vacancyScore > alpha) {
            if (depth == maxDepth) {
                bestPoint = candidate;
            }

            alpha = vacancyScore;

            valueType = zobrist::exact;
        }

        if (--limit < 0) {
            break;
        }
    }

    zobrist.insert(zobrist.hash(), valueType, depth, alpha);

    return alpha;
}

int Gobang::calculateScore(const QPoint &point)
{
    int score = 0;
    const std::array<int, 4> dx = {1, 0, 1, 1};
    const std::array<int, 4> dy = {0, 1, 1, -1};

    for (int i = 0; i < 4; ++i) {
        score += dScore(point, dx[i], dy[i]);
    }

    return score;
}

int Gobang::dScore(const QPoint &point, const int &dx, const int &dy)
{
    std::string blackLine;
    std::string whiteLine;
    int score = 0;

    for (int i = -5; i <= 5; ++i) {
        auto neighborhood = QPoint(point.x() + dx * i, point.y() + dy * i);

        if (!isLegal(neighborhood)) {
            continue;
        }

        if (neighborhood == point) {
            blackLine.push_back('1');
            whiteLine.push_back('1');

            continue;
        }

        switch (checkStone(neighborhood)) {
        case empty:
            blackLine.push_back('0');
            whiteLine.push_back('0');

            break;
        case black:
            blackLine.push_back('1');
            whiteLine.push_back(' ');

            break;
        case white:
            blackLine.push_back(' ');
            whiteLine.push_back('1');

            break;
        }
    }

    auto shapes = trie.parse_text(blackLine);

    for (const auto &shape : shapes) {
        score += static_cast<int>(shapeScoreMap.at(shape.get_keyword()));
    }

    shapes = trie.parse_text(whiteLine);

    for (const auto &shape : shapes) {
        score += static_cast<int>(shapeScoreMap.at(shape.get_keyword()));
    }

    return score;
}


int Gobang::evaluate(const stone &stone) const
{
    return stone == black ? blackTotalScore : whiteTotalScore;
}