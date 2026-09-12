// bankshot.cpp — ricochet aiming solver for Tanks Arena.
//
// A shell bounces once off a wall. Given a shooter and a target, this finds
// the firing angle that lands a hit, including shots that go around cover.
//
// Method: mirror the target across a wall plane. A straight line to the
// mirrored point crosses the wall at exactly the point where a real shell
// would bounce toward the real target. Same trick as a bank shot in pool.
//
// Build:  g++ -std=c++17 -O2 -o bankshot bankshot.cpp
// Run:    ./bankshot

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace tanks {

constexpr double CELL = 25.0;
constexpr int COLS = 14;
constexpr int ROWS = 14;
constexpr double FIELD_W = COLS * CELL;
constexpr double FIELD_H = ROWS * CELL;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline double dist(Vec2 a, Vec2 b) { return std::hypot(b.x - a.x, b.y - a.y); }

// A reflecting plane: the inner face of one arena border.
struct Mirror {
    const char* name;
    bool vertical;   // true = plane of constant x
    double pos;      // the plane's coordinate
    Vec2 normal;     // unit vector pointing into the playfield
};

const std::vector<Mirror> kMirrors = {
    {"left",   true,  CELL,            { 1.0,  0.0}},
    {"right",  true,  FIELD_W - CELL,  {-1.0,  0.0}},
    {"top",    false, CELL,            { 0.0,  1.0}},
    {"bottom", false, FIELD_H - CELL,  { 0.0, -1.0}},
};

class Arena {
public:
    explicit Arena(std::vector<std::string> rows) : rows_(std::move(rows)) {}

    bool wallAtCell(int col, int row) const {
        if (col < 0 || row < 0 || col >= COLS || row >= ROWS) return true;
        return rows_[row][col] == '#';
    }

    bool wallAt(Vec2 p) const {
        return wallAtCell(static_cast<int>(std::floor(p.x / CELL)),
                          static_cast<int>(std::floor(p.y / CELL)));
    }

    // Samples the segment finely enough that no 25px wall can slip between steps.
    bool clearPath(Vec2 a, Vec2 b) const {
        const double d = dist(a, b);
        const int steps = std::max(2, static_cast<int>(std::ceil(d / 2.0)));
        for (int i = 1; i < steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            if (wallAt({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t})) return false;
        }
        return true;
    }

    Vec2 centerOf(int col, int row) const {
        return {(col + 0.5) * CELL, (row + 0.5) * CELL};
    }

    std::vector<std::pair<char, Vec2>> markers() const {
        std::vector<std::pair<char, Vec2>> found;
        for (int r = 0; r < ROWS; ++r)
            for (int c = 0; c < COLS; ++c) {
                const char ch = rows_[r][c];
                if (ch != '#' && ch != '.') found.emplace_back(ch, centerOf(c, r));
            }
        return found;
    }

    void print() const {
        for (const std::string& row : rows_) std::printf("    %s\n", row.c_str());
    }

private:
    std::vector<std::string> rows_;
};

struct Solution {
    double angleRad = 0.0;
    double pathLength = 0.0;
    int bounces = 0;
    const char* wall = "none";
    Vec2 impact{};
};

inline Vec2 mirrorPoint(Vec2 p, const Mirror& m) {
    return m.vertical ? Vec2{2.0 * m.pos - p.x, p.y}
                      : Vec2{p.x, 2.0 * m.pos - p.y};
}

std::optional<Solution> solveDirect(const Arena& arena, Vec2 from, Vec2 to) {
    if (!arena.clearPath(from, to)) return std::nullopt;
    Solution s;
    s.angleRad = std::atan2(to.y - from.y, to.x - from.x);
    s.pathLength = dist(from, to);
    s.bounces = 0;
    return s;
}

// Tries every wall and keeps the shortest path that actually connects.
// Shorter matters: a long detour gives the target time to drive away.
std::optional<Solution> solveBank(const Arena& arena, Vec2 from, Vec2 to) {
    std::optional<Solution> best;

    for (const Mirror& m : kMirrors) {
        const Vec2 ghost = mirrorPoint(to, m);

        const double fromCoord = m.vertical ? from.x : from.y;
        const double ghostCoord = m.vertical ? ghost.x : ghost.y;
        const double span = ghostCoord - fromCoord;
        if (std::fabs(span) < 1e-9) continue;

        const double t = (m.pos - fromCoord) / span;
        if (t <= 0.0 || t >= 1.0) continue;  // plane is behind us or past the ghost

        Vec2 impact{from.x + (ghost.x - from.x) * t,
                    from.y + (ghost.y - from.y) * t};

        // The bounce has to land on the wall face, not past its corner.
        const double along = m.vertical ? impact.y : impact.x;
        const double limit = m.vertical ? FIELD_H : FIELD_W;
        if (along < CELL || along > limit - CELL) continue;

        // Step off the wall before tracing, or the wall itself blocks the ray.
        const Vec2 lift{impact.x + m.normal.x * 3.0, impact.y + m.normal.y * 3.0};
        if (!arena.clearPath(from, lift)) continue;
        if (!arena.clearPath(lift, to)) continue;

        Solution s;
        s.angleRad = std::atan2(ghost.y - from.y, ghost.x - from.x);
        s.pathLength = dist(from, impact) + dist(impact, to);
        s.bounces = 1;
        s.wall = m.name;
        s.impact = impact;

        if (!best || s.pathLength < best->pathLength) best = s;
    }
    return best;
}

std::optional<Solution> solve(const Arena& arena, Vec2 from, Vec2 to) {
    if (auto direct = solveDirect(arena, from, to)) return direct;
    return solveBank(arena, from, to);
}

inline double degrees(double rad) { return rad * 180.0 / M_PI; }

}  // namespace tanks

// ---------------------------------------------------------------- tests

namespace {

int checks = 0;
int failures = 0;

void check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("  FAIL  %s\n", label);
    } else {
        std::printf("  ok    %s\n", label);
    }
}

void runTests() {
    using namespace tanks;

    std::printf("Tests\n");

    // Reflecting twice across the same plane returns the original point.
    const Vec2 p{120.0, 80.0};
    const Vec2 twice = mirrorPoint(mirrorPoint(p, kMirrors[0]), kMirrors[0]);
    check(std::fabs(twice.x - p.x) < 1e-9 && std::fabs(twice.y - p.y) < 1e-9,
          "mirroring twice is the identity");

    const Arena open({
        "##############",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "#............#",
        "##############",
    });
    const Vec2 a = open.centerOf(2, 2);
    const Vec2 b = open.centerOf(11, 11);
    const auto openShot = solve(open, a, b);
    check(openShot && openShot->bounces == 0, "empty arena takes the direct shot");

    const Arena split({
        "##############",
        "#............#",
        "#............#",
        "#............#",
        "#......#.....#",
        "#......#.....#",
        "#......#.....#",
        "#.P....#...E.#",
        "#......#.....#",
        "#......#.....#",
        "#......#.....#",
        "#...E........#",
        "#............#",
        "##############",
    });
    const Vec2 shooter = split.centerOf(2, 7);
    const Vec2 behindCover = split.centerOf(11, 7);
    check(!solveDirect(split, shooter, behindCover),
          "cover blocks the straight shot");
    const auto bank = solveBank(split, shooter, behindCover);
    check(bank.has_value(), "a ricochet reaches the covered target");
    check(bank && bank->pathLength > dist(shooter, behindCover),
          "the ricochet path is longer than the blocked straight line");

    std::printf("  %d checks, %d failures\n\n", checks, failures);
}

void runDemo() {
    using namespace tanks;

    const Arena arena({
        "##############",
        "#............#",
        "#............#",
        "#............#",
        "#......#.....#",
        "#......#.....#",
        "#......#.....#",
        "#.P....#...A.#",
        "#......#.....#",
        "#......#.....#",
        "#......#.....#",
        "#...B........#",
        "#............#",
        "##############",
    });

    std::printf("Arena  (# wall, P shooter, A/B targets)\n");
    arena.print();
    std::printf("\n");

    Vec2 shooter{};
    std::vector<std::pair<char, Vec2>> targets;
    for (const auto& [ch, pos] : arena.markers()) {
        if (ch == 'P') shooter = pos;
        else targets.emplace_back(ch, pos);
    }

    std::printf("Shooter at (%.1f, %.1f)\n\n", shooter.x, shooter.y);
    std::printf("  target        position      shot      wall     angle     path\n");
    std::printf("  ------------------------------------------------------------\n");

    for (const auto& [ch, pos] : targets) {
        const auto shot = solve(arena, shooter, pos);
        if (!shot) {
            std::printf("  %-13c (%5.1f,%6.1f)  no solution\n", ch, pos.x, pos.y);
            continue;
        }
        std::printf("  %-13c (%5.1f,%6.1f)  %-8s  %-7s  %6.1f%c  %7.1f\n",
                    ch, pos.x, pos.y,
                    shot->bounces == 0 ? "direct" : "ricochet",
                    shot->wall,
                    degrees(shot->angleRad), 0xB0,
                    shot->pathLength);
    }
    std::printf("\n");
}

}  // namespace

int main() {
    runTests();
    runDemo();
    return failures == 0 ? 0 : 1;
}
