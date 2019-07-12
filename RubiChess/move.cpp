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


chessmove::chessmove(int from, int to, PieceCode promote, PieceCode capture, int ept, PieceCode piece)
{
    code = (piece << 28) | (ept << 20) | (capture << 16) | (promote << 12) | (from << 6) | to;
}


chessmove::chessmove(int from, int to, PieceCode promote, PieceCode capture, PieceCode piece)
{
    code = (piece << 28) | (capture << 16) | (promote << 12) | (from << 6) | to;
}


chessmove::chessmove(int from, int to, PieceCode piece)
{
    code = (piece << 28) | (from << 6) | to;
}


chessmove::chessmove(int from, int to, PieceCode capture, PieceCode piece)
{
    code = (piece << 28) | (capture << 16) | (from << 6) | to;
}


chessmove::chessmove()
{
    code = 0;
}


string chessmove::toString()
{
    char s[100];

    if (code == 0)
        return "(none)";

    int from, to;
    PieceCode promotion;
    from = GETFROM(code);
    to = GETTO(code);
    promotion = GETPROMOTION(code);

    sprintf_s(s, "%c%d%c%d%c", (from & 0x7) + 'a', ((from >> 3) & 0x7) + 1, (to & 0x7) + 'a', ((to >> 3) & 0x7) + 1, PieceChar(promotion, true));
    return s;
}


void chessmove::print()
{
    cout << toString();
}


chessmovelist::chessmovelist()
{
    length = 0;
}


string chessmovelist::toString()
{
    string s = "";
    for (int i = 0; i < length; i++)
    {
        s = s + move[i].toString() + " ";
    }
    return s;
}


string chessmovelist::toStringWithValue()
{
    string s = "";
    for (int i = 0; i < length; i++)
    {
        s = s + move[i].toString() + "(" + to_string((int)move[i].value) + ") ";
    }
    return s;
}

void chessmovelist::print()
{
    printf("%s", toString().c_str());
}


// Sorting for MoveSelector
void chessmovelist::sort()
{
    for (int i = 0; i < length - 1; i++)
    {
        for (int j = i + 1; j < length; j++)
            if (move[i].value < move[j].value)
                swap(move[i], move[j]);
    }
}


chessmovesequencelist::chessmovesequencelist()
{
    length = 0;
}


string chessmovesequencelist::toString()
{
    string s = "";
    for (int i = 0; i < length; i++)
    {
        s = s + move[i].toString() + " ";
    }
    return s;
}


void chessmovesequencelist::print()
{
    printf("%s", toString().c_str());
}


template <MoveType Mt>
void evaluateMoves(chessmovelist *ml, chessposition *pos, int16_t **cmptr)
{
    for (int i = 0; i < ml->length; i++)
    {
        uint32_t mc = ml->move[i].code;
        PieceCode piece = GETPIECE(mc);
        if (Mt == CAPTURE || (Mt == ALL && GETCAPTURE(mc)))
        {
            PieceCode capture = GETCAPTURE(mc);
            ml->move[i].value = (mvv[capture >> 1] | lva[piece >> 1]);
        }
        if (Mt == QUIET || (Mt == ALL && !GETCAPTURE(mc)))
        {
            ml->move[i].value = pos->history[piece & S2MMASK][GETFROM(mc)][GETTO(mc)];
            if (cmptr)
            {
                for (int j = 0; j < CMPLIES && cmptr[j]; j++)
                {
                    ml->move[i].value += cmptr[j][piece * 64 + GETTO(mc)];
                }
            }

        }
        if (GETPROMOTION(mc))
            ml->move[i].value += mvv[GETPROMOTION(mc) >> 1] - mvv[PAWN];
    }
}


void chessposition::getRootMoves()
{
    // Precalculating the list of legal moves didn't work well for some unknown reason but we need the number of legal moves in MultiPV mode
    chessmovelist movelist;
    prepareStack();
    movelist.length = getMoves(&movelist.move[0]);
    evaluateMoves<ALL>(&movelist, this, NULL);

    int bestval = SCOREBLACKWINS;
    rootmovelist.length = 0;
    excludemovestack[0] = 0; // FIXME: Not very nice; is it worth to do do singular testing in root search?
    for (int i = 0; i < movelist.length; i++)
    {
        if (playMove(&movelist.move[i]))
        {
            rootmovelist.move[rootmovelist.length++] = movelist.move[i];
            unplayMove(&movelist.move[i]);
            if (bestval < movelist.move[i].value)
            {
                defaultmove = movelist.move[i];
                bestval = movelist.move[i].value;
            }
        }
    }
}


void chessposition::tbFilterRootMoves()
{
    useTb = TBlargest;
    tbPosition = 0;
    useRootmoveScore = 0;
    if (POPCOUNT(occupied00[0] | occupied00[1]) <= TBlargest)
    {
        if ((tbPosition = root_probe(this))) {
            en.tbhits++;
            // The current root position is in the tablebases.
            // RootMoves now contains only moves that preserve the draw or win.

            // Do not probe tablebases during the search.
            useTb = 0;
        }
        else // If DTZ tables are missing, use WDL tables as a fallback
        {
            // Filter out moves that do not preserve a draw or win
            tbPosition = root_probe_wdl(this);
            // useRootmoveScore is set within root_probe_wdl
        }

        if (tbPosition)
        {
            // Sort the moves
            for (int i = 0; i < rootmovelist.length; i++)
            {
                for (int j = i + 1; j < rootmovelist.length; j++)
                {
                    if (rootmovelist.move[i] < rootmovelist.move[j])
                    {
                        swap(rootmovelist.move[i], rootmovelist.move[j]);
                    }
                }
            }
            defaultmove = rootmovelist.move[0];
        }
    }
}


uint32_t chessposition::shortMove2FullMove(uint16_t c)
{
    if (!c)
        return 0;

    int from = GETFROM(c);
    int to = GETTO(c);
    PieceCode pc = mailbox[from];
    if (!pc) // short move is wrong
        return 0;
    PieceCode capture = mailbox[to];
    PieceType p = pc >> 1;

    myassert(capture >= BLANK && capture <= BKING, this, 1, capture);
    myassert(pc >= WPAWN && pc <= BKING, this, 1, pc);

    int ept = 0;
    if (p == PAWN)
    {
        if (FILE(from) != FILE(to) && capture == BLANK)
        {
            // ep capture
            capture = pc ^ S2MMASK;
            ept = ISEPCAPTURE;
        }
        else if ((from ^ to) == 16 && (epthelper[to] & piece00[pc ^ 1]))
        {
            // double push enables epc
            ept = (from + to) / 2;
        }
    }

    uint32_t fc = (pc << 28) | (ept << 20) | (capture << 16) | c;
    if (moveIsPseudoLegal(fc))
        return fc;
    else
        return 0;
}


// FIXME: moveIsPseudoLegal gets more and more complicated with making it "thread safe"; maybe using 32bit for move in tp would be better?
bool chessposition::moveIsPseudoLegal(uint32_t c)
{
    if (!c)
        return false;

    int from = GETFROM(c);
    int to = GETTO(c);
    PieceCode pc = GETPIECE(c);
    PieceCode capture = GETCAPTURE(c);
    PieceType p = pc >> 1;
    unsigned int s2m = (pc & S2MMASK);

    myassert(pc >= WPAWN && pc <= BKING, this, 1, pc);

    // correct piece?
    if (mailbox[from] != pc)
        return false;

    // correct capture?
    if (mailbox[to] != capture && !GETEPCAPTURE(c))
        return false;

    // correct color of capture? capturing the king is illegal
    if (capture && (s2m == (capture & S2MMASK) || capture >= WKING))
        return false;

    myassert(capture >= BLANK && capture <= BQUEEN, this, 1, capture);

    // correct target for type of piece?
    if (!(movesTo(pc, from) & BITSET(to)) && (!ept || to != ept || p != PAWN))
        return false;

    // correct s2m?
    if (s2m != (state & S2MMASK))
        return false;

    // only pawn can promote
    if (GETPROMOTION(c) && p != PAWN)
        return false;

    if (p == PAWN)
    {
        // pawn specials
        if (((from ^ to) == 16))
        {
            // double push
            if (betweenMask[from][to] & (occupied00[0] | occupied00[1]))
                // blocked
                return false;

            // test if "making ep capture possible" is both true or false
            int ept = GETEPT(c);
            {
                if (!ept == (bool)(epthelper[to] & piece00[pc ^ 1]))
                    return false;
            }
        }
        else
        {
            // wrong ep capture
            if (GETEPCAPTURE(c) && ept != to)
                return false;

            // missing promotion
            if (RRANK(to, s2m) == 7 && !GETPROMOTION(c))
                return false;
        }
    }


    if (p == KING && (((from ^ to) & 3) == 2))
    {
        // test for correct castle
        if (isAttacked(from))
            return false;

        U64 occupied = occupied00[0] | occupied00[1];
        int s2m = state & S2MMASK;

        if (from > to)
        {
            //queen castle
            if (occupied & (s2m ? 0x0e00000000000000 : 0x000000000000000e)
                || isAttacked(from - 1)
                || isAttacked(from - 2)
                || !(state & QCMASK[s2m]))
            {
                return false;
            }
        }
        else
        {
            // king castle
            if (occupied & (s2m ? 0x6000000000000000 : 0x0000000000000060)
                || isAttacked(from + 1)
                || isAttacked(from + 2)
                || !(state & KCMASK[s2m]))
            {
                return false;
            }
        }
    }

    return true;
}


bool chessposition::moveGivesCheck(uint32_t c)
{
    int pc = GETPIECE(c);
    int me = pc & S2MMASK;
    int you = me ^ S2MMASK;
    int yourKing = kingpos[you];

    // test if moving piece gives check
    if (movesTo(pc, GETTO(c)) & BITSET(yourKing))
        return true;

#if 0
    // test for discovered check; seems a good idea but doesn't work, maybe too expensive for too few positives
    if (isAttackedByMySlider(yourKing, (occupied00[0] | occupied00[1]) ^ BITSET(GETTO(c)) ^ BITSET(GETFROM(c)), me))
        return true;
#endif

    return false;
}


void chessposition::playNullMove()
{
    movestack[mstop++].movecode = 0;
    state ^= S2MMASK;
    hash ^= zb.s2m;
    ply++;
    myassert(mstop < MAXMOVESEQUENCELENGTH, this, 1, mstop);
}


void chessposition::unplayNullMove()
{
    state ^= S2MMASK;
    hash ^= zb.s2m;
    ply--;
    mstop--;
    myassert(mstop >= 0, this, 1, mstop);
}


bool chessposition::playMove(chessmove *cm)
{
    int s2m = state & S2MMASK;
    int from = GETFROM(cm->code);
    int to = GETTO(cm->code);
    PieceCode pfrom = GETPIECE(cm->code);
    PieceType ptype = (pfrom >> 1);
    int eptnew = GETEPT(cm->code);

    PieceCode promote = GETPROMOTION(cm->code);
    PieceCode capture = GETCAPTURE(cm->code);

    myassert(!promote || (ptype == PAWN && RRANK(to, s2m) == 7), this, 4, promote, ptype, to, s2m);
    myassert(pfrom == mailbox[from], this, 3, pfrom, from, mailbox[from]);
    myassert(GETEPCAPTURE(cm->code) || capture == mailbox[to], this, 2, capture, mailbox[to]);

    halfmovescounter++;

    // Fix hash regarding capture
    if (capture != BLANK && !GETEPCAPTURE(cm->code))
    {
        hash ^= zb.boardtable[(to << 4) | capture];
        if ((capture >> 1) == PAWN)
            pawnhash ^= zb.boardtable[(to << 4) | capture];
        BitboardClear(to, capture);
        materialhash ^= zb.boardtable[(POPCOUNT(piece00[capture]) << 4) | capture];
        halfmovescounter = 0;
    }

    if (promote == BLANK)
    {
        mailbox[to] = pfrom;
        BitboardMove(from, to, pfrom);
    }
    else {
        mailbox[to] = promote;
        BitboardClear(from, pfrom);
        materialhash ^= zb.boardtable[(POPCOUNT(piece00[pfrom]) << 4) | pfrom];
        materialhash ^= zb.boardtable[(POPCOUNT(piece00[promote]) << 4) | promote];
        BitboardSet(to, promote);
        // just double the hash-switch for target to make the pawn vanish
        pawnhash ^= zb.boardtable[(to << 4) | mailbox[to]];
    }

    hash ^= zb.boardtable[(to << 4) | mailbox[to]];
    hash ^= zb.boardtable[(from << 4) | pfrom];

    mailbox[from] = BLANK;

    /* PAWN specials */
    if (ptype == PAWN)
    {
        pawnhash ^= zb.boardtable[(to << 4) | mailbox[to]];
        pawnhash ^= zb.boardtable[(from << 4) | pfrom];
        halfmovescounter = 0;

        if (ept && to == ept)
        {
            int epfield = (from & 0x38) | (to & 0x07);
            BitboardClear(epfield, (pfrom ^ S2MMASK));
            mailbox[epfield] = BLANK;
            hash ^= zb.boardtable[(epfield << 4) | (pfrom ^ S2MMASK)];
            pawnhash ^= zb.boardtable[(epfield << 4) | (pfrom ^ S2MMASK)];
            materialhash ^= zb.boardtable[(POPCOUNT(piece00[(pfrom ^ S2MMASK)]) << 4) | (pfrom ^ S2MMASK)];
        }
    }

    if (ptype == KING)
        kingpos[s2m] = to;

    // Here we can test the move for being legal
    if (isAttacked(kingpos[s2m]))
    {
        // Move is illegal; just do the necessary subset of unplayMove
        hash = movestack[mstop].hash;
        pawnhash = movestack[mstop].pawnhash;
        materialhash = movestack[mstop].materialhash;
        kingpos[s2m] = movestack[mstop].kingpos[s2m];
        halfmovescounter = movestack[mstop].halfmovescounter;
        mailbox[from] = pfrom;
        if (promote != BLANK)
        {
            BitboardClear(to, mailbox[to]);
            BitboardSet(from, pfrom);
        }
        else {
            BitboardMove(to, from, pfrom);
        }

        if (capture != BLANK)
        {
            if (ept && to == ept)
            {
                // special ep capture
                int epfield = (from & 0x38) | (to & 0x07);
                BitboardSet(epfield, capture);
                mailbox[epfield] = capture;
                mailbox[to] = BLANK;
            }
            else
            {
                BitboardSet(to, capture);
                mailbox[to] = capture;
            }
        }
        else {
            mailbox[to] = BLANK;
        }
        return false;
    }

    PREFETCH(&mh.table[materialhash & MATERIALHASHMASK]);

    // remove castle rights
    int oldcastle = (state & CASTLEMASK);
    state &= (castlerights[from] & castlerights[to]);
    if (ptype == KING)
    {
        // Store king position in pawn hash
        pawnhash ^= zb.boardtable[(from << 4) | pfrom] ^ zb.boardtable[(to << 4) | pfrom];

        /* handle castle */
        state &= (s2m ? ~(BQCMASK | BKCMASK) : ~(WQCMASK | WKCMASK));
        int c = castleindex[from][to];
        if (c)
        {
            int rookfrom = castlerookfrom[c];
            int rookto = castlerookto[c];

            BitboardMove(rookfrom, rookto, (PieceCode)(WROOK | s2m));
            mailbox[rookto] = (PieceCode)(WROOK | s2m);

            hash ^= zb.boardtable[(rookto << 4) | (PieceCode)(WROOK | s2m)];
            hash ^= zb.boardtable[(rookfrom << 4) | (PieceCode)(WROOK | s2m)];

            mailbox[rookfrom] = BLANK;
        }
    }

    PREFETCH(&pwnhsh->table[pawnhash & pwnhsh->sizemask]);

    state ^= S2MMASK;
    isCheckbb = isAttackedBy<OCCUPIED>(kingpos[s2m ^ S2MMASK], s2m);

    hash ^= zb.s2m;

    if (!(state & S2MMASK))
        fullmovescounter++;

    // Fix hash regarding ept
    hash ^= zb.ept[ept];
    ept = eptnew;
    hash ^= zb.ept[ept];

    // Fix hash regarding castle rights
    oldcastle ^= (state & CASTLEMASK);
    hash ^= zb.cstl[oldcastle];

    PREFETCH(&tp.table[hash & tp.sizemask]);

    ply++;
    rp.addPosition(hash);
    movestack[mstop++].movecode = cm->code;
    myassert(mstop < MAXMOVESEQUENCELENGTH, this, 1, mstop);

    return true;
}


void chessposition::unplayMove(chessmove *cm)
{
    int from = GETFROM(cm->code);
    int to = GETTO(cm->code);
    PieceCode pto = mailbox[to];
    PieceCode promote = GETPROMOTION(cm->code);
    PieceCode capture = GETCAPTURE(cm->code);
    int s2m;

    rp.removePosition(hash);
    ply--;

    mstop--;
    myassert(mstop >= 0, this, 1, mstop);
    // copy data from stack back to position
    memcpy(&state, &movestack[mstop], sizeof(chessmovestack));

    s2m = state & S2MMASK;
    if (promote != BLANK)
    {
        mailbox[from] = (PieceCode)(WPAWN | s2m);
        BitboardClear(to, pto);
        BitboardSet(from, (PieceCode)(WPAWN | s2m));
    }
    else {
        BitboardMove(to, from, pto);
        mailbox[from] = pto;
    }

    if (capture != BLANK)
    {
        if (ept && to == ept)
        {
            // special ep capture
            int epfield = (from & 0x38) | (to & 0x07);
            BitboardSet(epfield, capture);
            mailbox[epfield] = capture;
            mailbox[to] = BLANK;
        }
        else
        {
            BitboardSet(to, capture);
            mailbox[to] = capture;
        }
    }
    else {
        mailbox[to] = BLANK;
    }

    if ((pto >> 1) == KING)
    {
        int c = castleindex[from][to];
        if (c)
        {
            int rookfrom = castlerookfrom[c];
            int rookto = castlerookto[c];

            BitboardMove(rookto, rookfrom, (PieceCode)(WROOK | s2m));
            mailbox[rookfrom] = (PieceCode)(WROOK | s2m);
            mailbox[rookto] = BLANK;
        }
    }
}


// more advanced see respecting a variable threshold, quiet and promotion moves and faster xray attack handling
bool chessposition::see(uint32_t move, int threshold)
{
    int from = GETFROM(move);
    int to = GETTO(move);

    int value = GETTACTICALVALUE(move) - threshold;

    if (value < 0)
        // the move itself is not good enough to reach the threshold
        return false;

    int nextPiece = (ISPROMOTION(move) ? GETPROMOTION(move) : GETPIECE(move)) >> 1;

    value -= materialvalue[nextPiece];

    if (value >= 0)
        // the move is good enough even if the piece is recaptured
        return true;

    // Now things get a little more complicated...
    U64 seeOccupied = ((occupied00[0] | occupied00[1]) ^ BITSET(from)) | BITSET(to);
    U64 potentialRookAttackers = (piece00[WROOK] | piece00[BROOK] | piece00[WQUEEN] | piece00[BQUEEN]);
    U64 potentialBishopAttackers = (piece00[WBISHOP] | piece00[BBISHOP] | piece00[WQUEEN] | piece00[BQUEEN]);

    // Get attackers excluding the already moved piece
    U64 attacker = attackedByBB(to, seeOccupied) & seeOccupied;

    int s2m = (state & S2MMASK) ^ S2MMASK;

    while (true)
    {
        U64 nextAttacker = attacker & occupied00[s2m];
        // No attacker left => break
        if (!nextAttacker)
            break;

        // Find attacker with least value
        nextPiece = PAWN;
        while (!(nextAttacker & piece00[(nextPiece << 1) | s2m]))
            nextPiece++;

        // Simulate the move
        int attackerIndex;
        GETLSB(attackerIndex, nextAttacker & piece00[(nextPiece << 1) | s2m]);
        seeOccupied ^= BITSET(attackerIndex);

        // Add new shifting attackers but exclude already moved attackers using current seeOccupied
        if ((nextPiece & 0x1) || nextPiece == KING)  // pawn, bishop, queen, king
            attacker |= (MAGICBISHOPATTACKS(seeOccupied, to) & potentialBishopAttackers);
        if (nextPiece == ROOK || nextPiece == QUEEN || nextPiece == KING)
            attacker |= (MAGICROOKATTACKS(seeOccupied, to) & potentialRookAttackers);

        // Remove attacker
        attacker &= seeOccupied;

        s2m ^= S2MMASK;

        value = -value - 1 - materialvalue[nextPiece];

        if (value >= 0)
            break;
    }

    return (s2m ^ (state & S2MMASK));
}


int chessposition::getBestPossibleCapture()
{
    int me = state & S2MMASK;
    int you = me ^ S2MMASK;
    int captureval = 0;

    if (piece00[WQUEEN | you])
        captureval += materialvalue[QUEEN];
    else if (piece00[WROOK | you])
        captureval += materialvalue[ROOK];
    else if (piece00[WKNIGHT | you] || piece00[WBISHOP | you])
        captureval += materialvalue[KNIGHT];
    else if (piece00[WPAWN | you])
        captureval += materialvalue[PAWN];

    // promotion
    if (piece00[WPAWN | me] & RANK7(me))
        captureval += materialvalue[QUEEN] - materialvalue[PAWN];

    return captureval;
}



// Explicit template instantiation
// This avoids putting these definitions in header file
template void evaluateMoves<CAPTURE>(chessmovelist *, chessposition *, int16_t **);
template void evaluateMoves<QUIET>(chessmovelist *, chessposition *, int16_t **);
template void evaluateMoves<ALL>(chessmovelist *, chessposition *, int16_t **);
