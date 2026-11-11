#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QKeyEvent>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QSvgRenderer>
#include <QTimer>
#include <QWidget>
#include <array>
#include <optional>

// The model is deliberately independent from the widget: it owns the board
// and implements movement/merging rules, while GameWidget only draws and routes input.
enum class PetType { Cat, Parrot };

struct Pet {
    PetType type;
    int level = 1;
};

class GameModel {
public:
    static constexpr int Columns = 4;
    static constexpr int Rows = 4;
    using Board = std::array<std::array<std::optional<Pet>, Columns>, Rows>;

    GameModel() { reset(); }

    void reset() {
        for (auto &row : board_) for (auto &cell : row) cell.reset();
        score_ = 0;
        over_ = false;
        spawn();
    }

    bool move(int dx) {
        if (over_ || (dx != -1 && dx != 1)) return false;
        const int next = x_ + dx;
        if (next < 0 || next >= Columns || occupied(next, y_)) return false;
        x_ = next;
        return true;
    }

    // Called by the timer; returns true whenever the visible state changes.
    bool tick() {
        if (over_) return false;
        if (y_ + 1 < Rows && !occupied(x_, y_ + 1)) {
            ++y_;
            return true;
        }
        settle();
        spawn();
        return true;
    }

    void hardDrop() {
        if (over_) return;
        while (y_ + 1 < Rows && !occupied(x_, y_ + 1)) ++y_;
        settle();
        spawn();
    }

    const Board &board() const { return board_; }
    int activeX() const { return x_; }
    int activeY() const { return y_; }
    const Pet &activePet() const { return active_; }
    int score() const { return score_; }
    bool isOver() const { return over_; }

private:
    Board board_{};
    Pet active_{PetType::Cat, 1};
    int x_ = Columns / 2;
    int y_ = 0;
    int score_ = 0;
    bool over_ = false;

    bool occupied(int x, int y) const {
        return y >= 0 && y < Rows && board_[y][x].has_value();
    }

    void spawn() {
        x_ = Columns / 2;
        y_ = 0;
        // Cats are common; parrots add an occasional obstacle and special merge.
        active_ = QRandomGenerator::global()->bounded(4) == 0
                    ? Pet{PetType::Parrot, 1} : Pet{PetType::Cat, 1};
        if (occupied(x_, y_)) over_ = true;
    }

    void settle() {
        board_[y_][x_] = active_;
        stabilizeBoard();
    }

    bool inside(int x, int y) const { return x >= 0 && x < Columns && y >= 0 && y < Rows; }

    // Equal adjacent cats merge. A level-5 cat absorbs an adjacent parrot to
    // become level 6. Two adjacent parrots trigger the closest equal-level cat pair.
    bool resolveAt(int x, int y) {
        if (!board_[y][x]) return false;
        Pet &pet = *board_[y][x];
        constexpr int dx[4] = {1, -1, 0, 0};
        constexpr int dy[4] = {0, 0, 1, -1};
        if (pet.type == PetType::Cat) {
            for (int i = 0; i < 4; ++i) {
                const int nx = x + dx[i], ny = y + dy[i];
                if (!inside(nx, ny) || !board_[ny][nx]) continue;
                const Pet neighbor = *board_[ny][nx];
                if (neighbor.type == PetType::Cat && neighbor.level == pet.level && pet.level < 5) {
                    board_[ny][nx].reset();
                    ++pet.level;
                    score_ += pet.level * 10;
                    return true;
                }
                if (pet.level >= 5 && neighbor.type == PetType::Parrot) {
                    board_[ny][nx].reset();
                    ++pet.level;
                    score_ += 60;
                    return true;
                }
            }
        } else {
            for (int i = 0; i < 4; ++i) {
                const int nx = x + dx[i], ny = y + dy[i];
                if (inside(nx, ny) && board_[ny][nx] && board_[ny][nx]->type == PetType::Parrot) {
                    board_[ny][nx].reset();
                    board_[y][x].reset();
                    mergeClosestCats();
                    score_ += 25;
                    return true;
                }
            }
        }
        return false;
    }

    void applyGravity() {
        for (int x = 0; x < Columns; ++x) {
            int destination = Rows - 1;
            for (int y = Rows - 1; y >= 0; --y) {
                if (!board_[y][x]) continue;
                if (destination != y) {
                    board_[destination][x] = board_[y][x];
                    board_[y][x].reset();
                }
                --destination;
            }
        }
    }

    void stabilizeBoard() {
        // After a merge, let every remaining piece fall, then check whether
        // landing created another merge. Repeat until the board is stable.
        while (true) {
            applyGravity();
            bool merged = false;
            for (int y = Rows - 1; y >= 0 && !merged; --y) {
                for (int x = 0; x < Columns; ++x) {
                    if (resolveAt(x, y)) {
                        merged = true;
                        break;
                    }
                }
            }
            if (!merged) break;
        }
    }

    void mergeClosestCats() {
        int bestDistance = 1000, ax = -1, ay = -1, bx = -1, by = -1;
        for (int y1 = 0; y1 < Rows; ++y1) for (int x1 = 0; x1 < Columns; ++x1) {
            if (!board_[y1][x1] || board_[y1][x1]->type != PetType::Cat) continue;
            for (int y2 = y1; y2 < Rows; ++y2) for (int x2 = 0; x2 < Columns; ++x2) {
                if (y1 == y2 && x2 <= x1) continue;
                if (!board_[y2][x2] || board_[y2][x2]->type != PetType::Cat ||
                    board_[y1][x1]->level != board_[y2][x2]->level || board_[y1][x1]->level >= 5) continue;
                const int distance = qAbs(x1 - x2) + qAbs(y1 - y2);
                if (distance < bestDistance) {
                    bestDistance = distance; ax = x1; ay = y1; bx = x2; by = y2;
                }
            }
        }
        if (ax >= 0) {
            int &level = board_[ay][ax]->level;
            ++level;
            board_[by][bx].reset();
            score_ += level * 10;
        }
    }
};

class GameWidget final : public QWidget {
public:
    GameWidget() {
        setWindowTitle(QStringLiteral("Коты и попугаи — прототип"));
        setFixedSize(500, 570);
        setFocusPolicy(Qt::StrongFocus);
        timer_.setInterval(550);
        connect(&timer_, &QTimer::timeout, this, [this] { model_.tick(); update(); });
        timer_.start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor("#101827"));
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 20, QFont::Bold));
        p.drawText(QRect(20, 12, 460, 32), Qt::AlignCenter, QStringLiteral("КОТЫ И ПОПУГАИ"));
        p.setFont(QFont("Arial", 12));
        p.drawText(24, 66, QStringLiteral("Счёт: %1").arg(model_.score()));
        p.drawText(180, 66, QStringLiteral("← → двигать   ↓ быстрее   Пробел — сбросить"));

        const int cell = 88, left = (width() - cell * GameModel::Columns) / 2, top = 92;
        for (int y = 0; y < GameModel::Rows; ++y) for (int x = 0; x < GameModel::Columns; ++x) {
            const QRect r(left + x * cell + 2, top + y * cell + 2, cell - 4, cell - 4);
            p.setPen(QColor("#344256")); p.setBrush(QColor("#1b2738")); p.drawRoundedRect(r, 7, 7);
            if (model_.board()[y][x]) drawPet(p, r, *model_.board()[y][x]);
        }
        if (!model_.isOver()) {
            const QRect r(left + model_.activeX() * cell + 2, top + model_.activeY() * cell + 2, cell - 4, cell - 4);
            drawPet(p, r, model_.activePet());
        }
        p.setPen(QColor("#cbd5e1")); p.setFont(QFont("Arial", 11));
        p.drawText(QRect(20, 462, 460, 42), Qt::AlignCenter,
                   QStringLiteral("Коты одного уровня сливаются. Кот 5+ ест попугая."));
        p.drawText(QRect(20, 487, 460, 42), Qt::AlignCenter,
                   QStringLiteral("Пара попугаев соединяет ближайшую пару котов одного уровня."));
        if (model_.isOver()) {
            p.fillRect(QRect(35, 260, 430, 110), QColor(10, 18, 30, 225));
            p.setPen(QColor("#fb7185")); p.setFont(QFont("Arial", 24, QFont::Bold));
            p.drawText(QRect(35, 270, 430, 45), Qt::AlignCenter, QStringLiteral("Игра окончена"));
            p.setPen(Qt::white); p.setFont(QFont("Arial", 13));
            p.drawText(QRect(35, 315, 430, 35), Qt::AlignCenter, QStringLiteral("Нажмите R, чтобы начать заново"));
        }
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_R) model_.reset();
        else if (event->key() == Qt::Key_Left) model_.move(-1);
        else if (event->key() == Qt::Key_Right) model_.move(1);
        else if (event->key() == Qt::Key_Down) model_.tick();
        else if (event->key() == Qt::Key_Space) model_.hardDrop();
        update();
    }

private:
    GameModel model_;
    QTimer timer_;
    QHash<QString, QPixmap> imageCache_;

    void drawPet(QPainter &p, const QRect &rect, const Pet &pet) {
        const QColor color = pet.type == PetType::Cat ? QColor("#f6ad55") : QColor("#55c7a5");
        p.save();
        p.setPen(QPen(color.lighter(130), 2)); p.setBrush(color); p.drawRoundedRect(rect, 10, 10);
        const QString baseName = pet.type == PetType::Cat
            ? QStringLiteral("cat_%1").arg(pet.level)
            : QStringLiteral("parrot");
        const QString subdirectory = pet.type == PetType::Cat ? QStringLiteral("cats") : QStringLiteral("parrots");
        const QString key = subdirectory + QLatin1Char('/') + baseName;
        if (!imageCache_.contains(key)) {
            const QString appAssets = QCoreApplication::applicationDirPath() + QStringLiteral("/assets/");
            QString imagePath = appAssets + key + QStringLiteral(".svg");
            if (!QFile::exists(imagePath)) imagePath = appAssets + key + QStringLiteral(".png");
#ifdef PETGAME_ASSET_SOURCE_DIR
            if (!QFile::exists(imagePath)) {
                imagePath = QStringLiteral(PETGAME_ASSET_SOURCE_DIR) + QLatin1Char('/') + key + QStringLiteral(".svg");
                if (!QFile::exists(imagePath)) imagePath = QStringLiteral(PETGAME_ASSET_SOURCE_DIR) + QLatin1Char('/') + key + QStringLiteral(".png");
            }
#endif
            QPixmap image;
            if (imagePath.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive)) {
                QSvgRenderer renderer(imagePath);
                if (renderer.isValid()) {
                    image = QPixmap(128, 128);
                    image.fill(Qt::transparent);
                    QPainter imagePainter(&image);
                    renderer.render(&imagePainter);
                }
            } else {
                image.load(imagePath);
            }
            imageCache_.insert(key, image);
        }
        const QPixmap &image = imageCache_[key];
        if (!image.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(rect, 10, 10);
            p.setClipPath(clip);
            p.drawPixmap(rect, image);
            p.setClipping(false);
            p.setPen(Qt::white);
            p.setFont(QFont("Arial", 13, QFont::Bold));
            p.drawText(rect.adjusted(0, 0, -5, -5), Qt::AlignRight | Qt::AlignBottom,
                       pet.type == PetType::Cat ? QString::number(pet.level) : QString());
        } else {
            p.setPen(QColor("#18212e"));
            p.setFont(QFont("Arial", pet.type == PetType::Cat ? 17 : 14, QFont::Bold));
            p.drawText(rect.adjusted(2, 2, -2, -2), Qt::AlignCenter,
                       pet.type == PetType::Cat ? QStringLiteral("КОТ\n%1").arg(pet.level) : QStringLiteral("ПОПУГАЙ"));
        }
        p.restore();
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    GameWidget game;
    game.show();
    return app.exec();
}
