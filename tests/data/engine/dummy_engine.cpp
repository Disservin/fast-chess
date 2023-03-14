#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

bool contains(std::string_view haystack, std::string_view needle)
{
    return haystack.find(needle) != std::string::npos;
}

using namespace std;

int main()
{
    std::vector<std::string> moves = {"f2f3", "e7e5", "g2g4", "d8h4"};
    int moveIndex = 0;

    string cmd;
    while (true)
    {
        if (!getline(cin, cmd) && cin.eof())
            cmd = "quit";

        if (cmd == "quit")
        {
            break;
        }
        else if (cmd == "isready")
        {
            cout << "readyok" << endl;
        }
        else if (cmd == "uci")
        {
            cout << "line0" << endl;
            cout << "line1" << endl;
            cout << "uciok" << endl;
        }
        else if (cmd == "info")
        {
            int ms = 0;
            // clang-format off
            vector<string> infos{
                "info depth 1 seldepth 0 score cp 0 tbhits 0 nodes 20 nps 20000 hashfull 0 time 0 pv c2c4",
                "info depth 2 seldepth 1 score cp 36 tbhits 0 nodes 76 nps 76000 hashfull 0 time 0 pv c2c4 c7c5",
                "info depth 3 seldepth 2 score cp 10 tbhits 0 nodes 179 nps 179000 hashfull 0 time 0 pv c2c4 c7c5 g1f3",
                "info depth 4 seldepth 3 score cp 28 tbhits 0 nodes 363 nps 363000 hashfull 0 time 0 pv c2c4 c7c5 g1f3 g8f6",
                "info depth 5 seldepth 4 score cp 5 tbhits 0 nodes 616 nps 616000 hashfull 0 time 0 pv c2c4 c7c5 g1f3 g8f6 b1c3",
                "info depth 6 seldepth 5 score cp 20 tbhits 0 nodes 1539 nps 769000 hashfull 0 time 1 pv c2c4 g7g6 b1c3 c7c5 g1f3 f8g7",
                "info depth 7 seldepth 6 score cp 22 tbhits 0 nodes 3056 nps 1528000 hashfull 0 time 1 pv c2c4 g7g6 g1f3 f8g7 d2d4 g8f6 b1c3",
                "info depth 8 seldepth 7 score cp 21 tbhits 0 nodes 11016 nps 2203000 hashfull 2 time 4 pv g1f3 g8f6 g2g3 g7g6 f1g2 f8g7 e1g1 e8g8",
                "info depth 9 seldepth 8 score cp 24 tbhits 0 nodes 17258 nps 2465000 hashfull 2 time 6 pv g1f3 g8f6 c2c4 g7g6 b1c3 f8g7 d2d4 e8g8 e2e4",
                "info depth 10 seldepth 9 score cp 35 tbhits 0 nodes 35077 nps 2923000 hashfull 5 time 11 pv e2e4 c7c5 g1f3 g7g6 b1c3 f8g7 d2d4 c5d4 f3d4 g8f6",
                "info depth 11 seldepth 12 score cp 27 tbhits 0 nodes 48611 nps 2859000 hashfull 7 time 16 pv e2e4 c7c5 g1f3 g7g6 b1c3 b8c6 d2d4 c5d4 f3d4 f8g7 d4b5",
                "info depth 12 seldepth 12 score cp 24 tbhits 0 nodes 75709 nps 3028000 hashfull 9 time 24 pv e2e4 c7c5 g1f3 b8c6 b1c3 g8f6 d2d4 c5d4 f3d4 g7g6 c1e3 f8g7",
                "info depth 13 seldepth 13 score cp 30 tbhits 0 nodes 114590 nps 3183000 hashfull 14 time 35 pv e2e4 c7c5 g1f3 b8c6 f1b5 g8f6 e4e5 f6d5 e1g1 d5c7 b5c6 b7c6 c2c4",
                "info depth 14 seldepth 13 score cp 30 tbhits 0 nodes 166040 nps 3193000 hashfull 21 time 51 pv e2e4 c7c5 g1f3 e7e6 b1c3 b8c6 d2d4 c5d4 f3d4 g8f6 d4b5 d7d5 e4d5 e6d5",
                "info depth 15 seldepth 18 score cp 17 tbhits 0 nodes 406337 nps 3224000 hashfull 59 time 125 pv e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 f1d3 c7c5 e4d5 f6d5 c3d5 d8d5 g1f3 c5d4 e1g1",
                "info depth 16 seldepth 14 score cp 17 tbhits 0 nodes 532606 nps 3247000 hashfull 73 time 163 pv e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 f1d3 c7c5 e4d5 f6d5 c3d5 d8d5 g1f3 c5d4",
                "info depth 17 seldepth 21 score cp 16 tbhits 0 nodes 932682 nps 3216000 hashfull 142 time 289 pv e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 c5d4 c3d4 b8c6 g1f3 g8e7 f1e2 c8d7 e1g1 e7f5 b1c3",
                "info depth 18 seldepth 16 score cp 16 tbhits 0 nodes 1249667 nps 3220000 hashfull 186 time 387 pv e2e4 e7e6 d2d4 d7d5 e4e5 c7c5 c2c3 c5d4 c3d4 b8c6 g1f3 g8e7 f1e2 c8d7 e1g1 e7f5 b1c3",
                "info depth 19 seldepth 20 score cp 28 tbhits 0 nodes 1731549 nps 3206000 hashfull 265 time 539 pv e2e4 e7e6 d2d4 d7d5 b1d2 f8e7 g1f3 g8f6 e4e5 f6d7 c2c3 c7c5 f1d3 a7a6 e1g1 e8g8 f1e1 c5d4 c3d4",
                "info depth 20 seldepth 22 score cp 28 tbhits 0 nodes 2311898 nps 3202000 hashfull 330 time 721 pv e2e4 e7e6 d2d4 d7d5 b1d2 c7c5 e4d5 d8d5 g1f3 c5d4 f1c4 d5d8 e1g1 g8f6 f1e1 f8e7 c4b5 b8c6 d2b3",
                "info depth 21 seldepth 24 score cp 25 tbhits 0 nodes 7506213 nps 3169000 hashfull 745 time 2367 pv d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 g2g3 f8b4 c1d2 b4e7 f1g2 e8g8 e1g1 c7c6 d1c2 f8e8 b2b3 b7b6 f1c1 c8b7",
                "info depth 22 seldepth 24 score cp 17 tbhits 0 nodes 9051658 nps 3166000 hashfull 799 time 2858 pv d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 g2g3 a7a6 b1d2 f8e7 f1g2 e8g8 e1g1 b7b6 d1c2 c8b7 f1d1 f8e8 f3e5 b8d7",
                "info depth 23 seldepth 24 score cp 20 tbhits 0 nodes 11116018 nps 3164000 hashfull 868 time 3512 pv d2d4 g8f6 c2c4 e7e6 g1f3 d7d5 g2g3 a7a6 b1d2 f8e7 f1g2 e8g8 e1g1 b7b6 d1c2 c8b7 f1d1 d5c4 d2c4 b7e4 c2b3"};
            // clang-format on

            for (const auto &info : infos)
            {
                cout << info << endl;
                this_thread::sleep_for(chrono::milliseconds(ms++));
            }
        }
        else if (cmd == "sleep")
        {
            this_thread::sleep_for(chrono::milliseconds(1000));

            cout << "done" << endl;
        }
        else if (contains(cmd, "position"))
        {
            for (int i = moves.size() - 1; i >= 0; i--)
            {
                if (contains(cmd, moves[i]))
                {
                    moveIndex = i + 1;
                    break;
                }
            }
        }
        else if (contains(cmd, "go"))
        {
            std::cout << "bestmove " << moves[moveIndex] << std::endl;
        }
    }

    return 0;
}