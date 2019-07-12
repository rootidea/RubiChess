/*
  RubiChess is a UCI chess playing engine by Andreas Matthies.

  RubiChess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  RubiChess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "RubiChess.h"

searchthread::searchthread()
{
    pwnhsh = NULL;
}

searchthread::~searchthread()
{
    delete pwnhsh;
}


engine::engine()
{
    initBitmaphelper();

    setOption("Threads", "1");  // order is important as the pawnhash depends on Threads > 0
    setOption("hash", "256");
    setOption("Move Overhead", "50");
    setOption("MultiPV", "1");
    setOption("Ponder", "false");
    setOption("SyzygyPath", "<empty>");
    setOption("Syzygy50MoveRule", "true");

#ifdef _WIN32
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency = f.QuadPart;
#else
    frequency = 1000000000LL;
#endif
}

engine::~engine()
{
    setOption("SyzygyPath", "<empty>");
    delete[] sthread;
}

void engine::allocPawnhash()
{
    for (int i = 0; i < Threads; i++)
    {
        delete sthread[i].pwnhsh;
        sthread[i].pos.pwnhsh = sthread[i].pwnhsh = new Pawnhash(sizeOfPh);
    }
}


void engine::allocThreads()
{
    delete[] sthread;
    sthread = new searchthread[Threads];
    for (int i = 0; i < Threads; i++)
    {
        sthread[i].index = i;
        sthread[i].searchthreads = sthread;
        sthread[i].numofthreads = Threads;
    }
    allocPawnhash();
}


void engine::prepareThreads()
{
    sthread[0].pos.bestmovescore[0] = NOSCORE;
    sthread[0].pos.bestmove.code = 0;
    sthread[0].pos.nodes = 0;
    sthread[0].pos.nullmoveply = 0;
    sthread[0].pos.nullmoveside = 0;
    for (int i = 1; i < Threads; i++)
    {
        sthread[i].pos = sthread[0].pos;
        sthread[i].pos.pwnhsh = sthread[i].pwnhsh;
        sthread[i].pos.threadindex = i;
        // early reset of variables that are important for bestmove selection
        sthread[i].pos.bestmovescore[0] = NOSCORE;
        sthread[i].pos.bestmove.code = 0;
        sthread[i].pos.nodes = 0;
        sthread[i].pos.nullmoveply = 0;
        sthread[i].pos.nullmoveside = 0;
    }
}


U64 engine::getTotalNodes()
{
    U64 nodes = 0;
    for (int i = 0; i < Threads; i++)
        nodes += sthread[i].pos.nodes;

    return nodes;
}


void engine::setOption(string sName, string sValue)
{
    bool resetTp = false;
    bool resetTh = false;
    int newint;
    string lValue = sValue;
    transform(sName.begin(), sName.end(), sName.begin(), ::tolower);
    transform(lValue.begin(), lValue.end(), lValue.begin(), ::tolower);
    if (sName == "clear hash")
        tp.clean();
    if (sName == "ponder")
    {
        ponder = (lValue == "true");
    }
    if (sName == "multipv")
    {
        newint = stoi(sValue);
        if (newint >= 1 && newint <= MAXMULTIPV)
            MultiPV = newint;
    }
    if (sName == "threads")
    {
        newint = stoi(sValue);
        if (newint >= 1 && newint <= MAXTHREADS && newint != Threads)
        {
            Threads = newint;
            resetTh = true;
        }
    }
    if (sName == "hash")
    {
        newint = stoi(sValue);
        if (newint < 1)
            // at least a small hash table is required
            return;
        if (sizeOfTp != newint)
        {
            sizeOfTp = newint;
            resetTp = true;
        }
    }
    if (resetTp && sizeOfTp)
    {
        int newRestSizeTp = tp.setSize(sizeOfTp);
        if (restSizeOfTp != newRestSizeTp)
        {
            restSizeOfTp = newRestSizeTp;
            resetTh = true;
        }
    }
    if (resetTh)
    {
        sizeOfPh = min(128, max(16, restSizeOfTp / Threads));
        allocThreads();
    }
    if (sName == "move overhead")
    {
        newint = stoi(sValue);
        if (newint < 0 || newint > 5000)
            return;
        moveOverhead = newint;
    }
    if (sName == "syzygypath")
    {
        SyzygyPath = sValue;
        init_tablebases((char *)SyzygyPath.c_str());
    }
    if (sName == "syzygy50moverule")
    {
        Syzygy50MoveRule = (lValue == "true");
    }
}

static void waitForSearchGuide(thread **th)
{
    if (*th)
    {
        if ((*th)->joinable())
            (*th)->join();
        delete *th;
    }
    *th = nullptr;
}

void engine::communicate(string inputstring)
{
    string fen = STARTFEN;
    vector<string> moves;
    vector<string> searchmoves;
    vector<string> commandargs;
    GuiToken command = UNKNOWN;
    size_t ci, cs;
    bool bGetName, bGetValue;
    string sName, sValue;
    bool bMoves;
    thread *searchguidethread = nullptr;
    bool pendingisready = false;
    bool pendingposition = (inputstring == "");
    do
    {
        if (stopLevel >= ENGINESTOPIMMEDIATELY)
        {
            waitForSearchGuide(&searchguidethread);
        }
        if (pendingisready || pendingposition)
        {
            if (pendingposition)
            {
                // new position first stops current search
                if (stopLevel < ENGINESTOPIMMEDIATELY)
                {
                    stopLevel = ENGINESTOPIMMEDIATELY;
                    waitForSearchGuide(&searchguidethread);
                }
                chessposition *rootpos = &sthread[0].pos;
                rootpos->getFromFen(fen.c_str());
                U64 hashlist[MAXMOVESEQUENCELENGTH];
                hashlist[0] = rootpos->hash;
                int hashlistlength = 1;
                for (vector<string>::iterator it = moves.begin(); it != moves.end(); ++it)
                {
                    if (!rootpos->applyMove(*it))
                        printf("info string Alarm! Zug %s nicht anwendbar (oder Enginefehler)\n", (*it).c_str());
                    if (rootpos->halfmovescounter == 0)
                    {
                        // Remove the positions from repetition table to avoid wrong values by hash collisions
                        for (int i = 0; i < hashlistlength; i++)
                            rootpos->rp.removePosition(hashlist[i]);
                        hashlistlength = 0;
                    }

                    hashlist[hashlistlength++] = rootpos->hash;
                }
                rootpos->rootheight = rootpos->mstop;
                rootpos->ply = 0;
                rootpos->getRootMoves();
                rootpos->tbFilterRootMoves();
                prepareThreads();
                if (debug)
                {
                    rootpos->print();
                }
                pendingposition = false;
            }
            if (pendingisready)
            {
                myUci->send("readyok\n");
                pendingisready = false;
            }
        }
        else {
            commandargs.clear();
            command = myUci->parse(&commandargs, inputstring);  // blocking!!
            ci = 0;
            cs = commandargs.size();
            switch (command)
            {
            case UCIDEBUG:
                if (ci < cs)
                {
#ifdef SDEBUG
                    chessposition *rootpos = &sthread[0].pos;
#endif
                    if (commandargs[ci] == "on")
                        debug = true;
                    else if (commandargs[ci] == "off")
                        debug = false;
#ifdef SDEBUG
                    else if (commandargs[ci] == "this")
                        rootpos->debughash = rootpos->hash;
                    else if (commandargs[ci] == "pv")
                    {
                        rootpos->debugOnlySubtree = false;
                        rootpos->debugRecursive = false;
                        int i = 0;
                        while (++ci < cs)
                        {
                            string s = commandargs[ci];
                            if (s == "recursive")
                            {
                                rootpos->debugRecursive = true;
                                continue;
                            }
                            if (s == "sub")
                            {
                                rootpos->debugOnlySubtree = true;
                                continue;
                            }
                            if (s.size() < 4)
                                continue;
                            int from = AlgebraicToIndex(s);
                            int to = AlgebraicToIndex(&s[2]);
                            int promotion = (s.size() <= 4) ? BLANK : (GetPieceType(s[4]) << 1); // Remember: S2m is missing here
                            rootpos->pvdebug[i++] = to | (from << 6) | (promotion << 12);
                        }
                        rootpos->pvdebug[i] = 0;
                    }
#endif
                }
                break;
            case UCI:
                myUci->send("id name %s\n", name);
                myUci->send("id author %s\n", author);
                myUci->send("option name Clear Hash type button\n");
                myUci->send("option name Hash type spin default 256 min 1 max 1048576\n");
                myUci->send("option name Move Overhead type spin default 50 min 0 max 5000\n");
                myUci->send("option name MultiPV type spin default 1 min 1 max %d\n", MAXMULTIPV);
                myUci->send("option name Ponder type check default false\n");
                myUci->send("option name SyzygyPath type string default <empty>\n");
                myUci->send("option name Syzygy50MoveRule type check default true\n");
                myUci->send("option name Threads type spin default 1 min 1 max 128\n");
                myUci->send("uciok\n", author);
                break;
            case UCINEWGAME:
                // invalidate hash
                tp.clean();
                sthread[0].pos.lastbestmovescore = NOSCORE;
                break;
            case SETOPTION:
                if (en.stopLevel < ENGINESTOPPED)
                {
                    myUci->send("info string Changing option while searching is not supported.\n");
                    break;
                }
                bGetName = bGetValue = false;
                sName = sValue = "";
                while (ci < cs)
                {
                    string sLower = commandargs[ci];
                    transform(sLower.begin(), sLower.end(), sLower.begin(), ::tolower);

                    if (sLower == "name")
                    {
                        setOption(sName, sValue);
                        bGetName = true;
                        bGetValue = false;
                        sName = "";
                    }
                    else if (sLower == "value")
                    {
                        bGetValue = true;
                        bGetName = false;
                        sValue = "";
                    }
                    else if (bGetName)
                    {
                        if (sName != "")
                            sName += " ";
                        sName += commandargs[ci];
                    }
                    else if (bGetValue)
                    {
                        if (sValue != "")
                            sValue += " ";
                        sValue += commandargs[ci];
                    }
                    ci++;
                }
                setOption(sName, sValue);
                break;
            case ISREADY:
                pendingisready = true;
                break;
            case POSITION:
                if (cs == 0)
                    break;
                bMoves = false;
                moves.clear();
                fen = "";

                if (commandargs[ci] == "startpos")
                {
                    ci++;
                    fen = STARTFEN;
                }
                else if (commandargs[ci] == "fen")
                {
                    while (++ci < cs && commandargs[ci] != "moves")
                        fen = fen + commandargs[ci] + " ";
                }
                while (ci < cs)
                {
                    if (commandargs[ci] == "moves")
                    {
                        bMoves = true;
                    }
                    else if (bMoves)
                    {
                        moves.push_back(commandargs[ci]);
                    }
                    ci++;
                }

                pendingposition = (fen != "");
                break;
            case GO:
                resetPonder();
                searchmoves.clear();
                wtime = btime = winc = binc = movestogo = mate = maxdepth = 0;
                maxnodes = 0ULL;
                infinite = false;
                while (ci < cs)
                {
                    if (commandargs[ci] == "searchmoves")
                    {
                        while (++ci < cs && AlgebraicToIndex(commandargs[ci]) < 64 && AlgebraicToIndex(&commandargs[ci][2]) < 64)
                            searchmoves.push_back(commandargs[ci]);
                    }

                    else if (commandargs[ci] == "wtime")
                    {
                        if (++ci < cs)
                            wtime = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "btime")
                    {
                        if (++ci < cs)
                            btime = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "winc")
                    {
                        if (++ci < cs)
                            winc = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "binc")
                    {
                        if (++ci < cs)
                            binc = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "movetime")
                    {
                        movestogo = 1;
                        winc = binc = 0;
                        if (++ci < cs)
                            wtime = btime = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "movestogo")
                    {
                        if (++ci < cs)
                            movestogo = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "nodes")
                    {
                        if (++ci < cs)
                            maxnodes = stoull(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "mate")
                    {
                        if (++ci < cs)
                            mate = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "depth")
                    {
                        if (++ci < cs)
                            maxdepth = stoi(commandargs[ci++]);
                    }
                    else if (commandargs[ci] == "infinite")
                    {
                        infinite = true;
                        ci++;
                    }
                    else if (commandargs[ci] == "ponder")
                    {
                        pondersearch = PONDERING;
                        ci++;
                    }
                    else
                        ci++;
                }
                isWhite = (sthread[0].pos.w2m());
                stopLevel = ENGINERUN;
                searchguidethread = new thread(&searchguide);
                if (inputstring != "")
                {
                    // bench mode; wait for end of search
                    waitForSearchGuide(&searchguidethread);
                }
                break;
            case PONDERHIT:
                HitPonder();
                break;
            case STOP:
            case QUIT:
                stopLevel = ENGINESTOPIMMEDIATELY;
                break;
            case EVAL:
                sthread[0].pos.getEval<TRACE>();
                break;
            default:
                break;
            }
        }
    } while (command != QUIT && (inputstring == "" || pendingposition));
    waitForSearchGuide(&searchguidethread);
}

