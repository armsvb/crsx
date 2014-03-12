/* Generated By:JavaCC: Do not edit this line. TextTokenManager.java */
package net.sf.crsx.text;

/** Token Manager. */
public class TextTokenManager implements TextConstants
{

  /** Debug output. */
  public  java.io.PrintStream debugStream = System.out;
  /** Set debug output. */
  public  void setDebugStream(java.io.PrintStream ds) { debugStream = ds; }
private final int jjStopStringLiteralDfa_0(int pos, long active0)
{
   switch (pos)
   {
      default :
         return -1;
   }
}
private final int jjStartNfa_0(int pos, long active0)
{
   return jjMoveNfa_0(jjStopStringLiteralDfa_0(pos, active0), pos + 1);
}
private int jjStopAtPos(int pos, int kind)
{
   jjmatchedKind = kind;
   jjmatchedPos = pos;
   return pos + 1;
}
private int jjMoveStringLiteralDfa0_0()
{
   switch(curChar)
   {
      case 182:
         return jjStopAtPos(0, 1);
      case 8220:
         return jjStopAtPos(0, 4);
      case 8221:
         return jjStopAtPos(0, 5);
      case 8224:
         return jjStopAtPos(0, 6);
      case 8225:
         return jjStopAtPos(0, 7);
      case 8743:
         return jjStopAtPos(0, 8);
      case 10214:
         return jjStopAtPos(0, 2);
      case 10215:
         return jjStopAtPos(0, 3);
      default :
         return jjMoveNfa_0(2, 0);
   }
}
static final long[] jjbitVec0 = {
   0xffffff7afffffffeL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec2 = {
   0x0L, 0x0L, 0xf7bff7ffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec3 = {
   0xf9fffffcccffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec4 = {
   0xffffff7fffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec5 = {
   0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xfffffc3fffffffffL
};
static final long[] jjbitVec6 = {
   0x0L, 0x0L, 0x80000000000L, 0x0L
};
static final long[] jjbitVec7 = {
   0xfffffffffffffffeL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec8 = {
   0x0L, 0x0L, 0xf7fff7ffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec9 = {
   0x0L, 0x0L, 0x800000000000000L, 0x0L
};
static final long[] jjbitVec10 = {
   0x200000000000000L, 0x0L, 0x0L, 0x0L
};
static final long[] jjbitVec11 = {
   0xfffffffefffffffeL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec12 = {
   0x0L, 0x0L, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec13 = {
   0xf9ffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec14 = {
   0x400000000000000L, 0x0L, 0x0L, 0x0L
};
static final long[] jjbitVec15 = {
   0x1000000L, 0x0L, 0x0L, 0x0L
};
static final long[] jjbitVec16 = {
   0xfffffffffcffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec17 = {
   0x2000000L, 0x0L, 0x0L, 0x0L
};
static final long[] jjbitVec18 = {
   0x0L, 0x0L, 0x0L, 0x10000000000L
};
static final long[] jjbitVec19 = {
   0xffffff7ffffffffeL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL
};
static final long[] jjbitVec20 = {
   0xffffffffffffffffL, 0xffffffffffffffffL, 0xffffffffffffffffL, 0xfffffcffffffffffL
};
static final long[] jjbitVec21 = {
   0x0L, 0x0L, 0x0L, 0x20000000000L
};
private int jjMoveNfa_0(int startState, int curPos)
{
   int startsAt = 0;
   jjnewStateCnt = 14;
   int i = 1;
   jjstateSet[0] = startState;
   int kind = 0x7fffffff;
   for (;;)
   {
      if (++jjround == 0x7fffffff)
         ReInitRounds();
      if (curChar < 64)
      {
         long l = 1L << curChar;
         do
         {
            switch(jjstateSet[--i])
            {
               case 2:
                  if ((0xffffffffffffcbffL & l) != 0L)
                  {
                     if (kind > 9)
                        kind = 9;
                     jjCheckNAdd(0);
                  }
                  else if ((0x3400L & l) != 0L)
                  {
                     if (kind > 10)
                        kind = 10;
                     jjCheckNAdd(1);
                  }
                  break;
               case 0:
                  if ((0xffffffffffffcbffL & l) == 0L)
                     break;
                  if (kind > 9)
                     kind = 9;
                  jjCheckNAdd(0);
                  break;
               case 1:
                  if ((0x3400L & l) == 0L)
                     break;
                  if (kind > 10)
                     kind = 10;
                  jjCheckNAdd(1);
                  break;
               case 3:
                  jjAddStates(0, 1);
                  break;
               case 6:
                  jjAddStates(2, 3);
                  break;
               case 9:
                  jjAddStates(4, 5);
                  break;
               case 12:
                  jjAddStates(6, 7);
                  break;
               default : break;
            }
         } while(i != startsAt);
      }
      else if (curChar < 128)
      {
         long l = 1L << (curChar & 077);
         do
         {
            switch(jjstateSet[--i])
            {
               case 2:
               case 0:
                  if (kind > 9)
                     kind = 9;
                  jjCheckNAdd(0);
                  break;
               case 3:
                  jjAddStates(0, 1);
                  break;
               case 6:
                  jjAddStates(2, 3);
                  break;
               case 9:
                  jjAddStates(4, 5);
                  break;
               case 12:
                  jjAddStates(6, 7);
                  break;
               default : break;
            }
         } while(i != startsAt);
      }
      else
      {
         int hiByte = (int)(curChar >> 8);
         int i1 = hiByte >> 6;
         long l1 = 1L << (hiByte & 077);
         int i2 = (curChar & 0xff) >> 6;
         long l2 = 1L << (curChar & 077);
         do
         {
            switch(jjstateSet[--i])
            {
               case 2:
                  if (jjCanMove_0(hiByte, i1, i2, l1, l2))
                  {
                     if (kind > 9)
                        kind = 9;
                     jjCheckNAdd(0);
                  }
                  if (jjCanMove_1(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(3, 4);
                  if (jjCanMove_4(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(6, 7);
                  if (jjCanMove_7(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(9, 10);
                  if (jjCanMove_10(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(12, 13);
                  break;
               case 0:
                  if (!jjCanMove_0(hiByte, i1, i2, l1, l2))
                     break;
                  if (kind > 9)
                     kind = 9;
                  jjCheckNAdd(0);
                  break;
               case 3:
                  if (jjCanMove_2(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(3, 4);
                  break;
               case 4:
                  if (jjCanMove_3(hiByte, i1, i2, l1, l2) && kind > 11)
                     kind = 11;
                  break;
               case 5:
                  if (jjCanMove_4(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(6, 7);
                  break;
               case 6:
                  if (jjCanMove_5(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(6, 7);
                  break;
               case 7:
                  if (jjCanMove_6(hiByte, i1, i2, l1, l2) && kind > 11)
                     kind = 11;
                  break;
               case 8:
                  if (jjCanMove_7(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(9, 10);
                  break;
               case 9:
                  if (jjCanMove_8(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(9, 10);
                  break;
               case 10:
                  if (jjCanMove_9(hiByte, i1, i2, l1, l2) && kind > 11)
                     kind = 11;
                  break;
               case 11:
                  if (jjCanMove_10(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(12, 13);
                  break;
               case 12:
                  if (jjCanMove_11(hiByte, i1, i2, l1, l2))
                     jjCheckNAddTwoStates(12, 13);
                  break;
               case 13:
                  if (jjCanMove_12(hiByte, i1, i2, l1, l2) && kind > 11)
                     kind = 11;
                  break;
               default : break;
            }
         } while(i != startsAt);
      }
      if (kind != 0x7fffffff)
      {
         jjmatchedKind = kind;
         jjmatchedPos = curPos;
         kind = 0x7fffffff;
      }
      ++curPos;
      if ((i = jjnewStateCnt) == (startsAt = 14 - (jjnewStateCnt = startsAt)))
         return curPos;
      try { curChar = input_stream.readChar(); }
      catch(java.io.IOException e) { return curPos; }
   }
}
static final int[] jjnextStates = {
   3, 4, 6, 7, 9, 10, 12, 13, 
};
private static final boolean jjCanMove_0(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec2[i2] & l2) != 0L);
      case 32:
         return ((jjbitVec3[i2] & l2) != 0L);
      case 34:
         return ((jjbitVec4[i2] & l2) != 0L);
      case 39:
         return ((jjbitVec5[i2] & l2) != 0L);
      default :
         if ((jjbitVec0[i1] & l1) != 0L)
            return true;
         return false;
   }
}
private static final boolean jjCanMove_1(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec6[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_2(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec8[i2] & l2) != 0L);
      default :
         if ((jjbitVec7[i1] & l1) != 0L)
            return true;
         return false;
   }
}
private static final boolean jjCanMove_3(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec9[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_4(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 32:
         return ((jjbitVec10[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_5(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec12[i2] & l2) != 0L);
      case 32:
         return ((jjbitVec13[i2] & l2) != 0L);
      default :
         if ((jjbitVec11[i1] & l1) != 0L)
            return true;
         return false;
   }
}
private static final boolean jjCanMove_6(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 32:
         return ((jjbitVec14[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_7(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 32:
         return ((jjbitVec15[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_8(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec12[i2] & l2) != 0L);
      case 32:
         return ((jjbitVec16[i2] & l2) != 0L);
      default :
         if ((jjbitVec11[i1] & l1) != 0L)
            return true;
         return false;
   }
}
private static final boolean jjCanMove_9(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 32:
         return ((jjbitVec17[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_10(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 39:
         return ((jjbitVec18[i2] & l2) != 0L);
      default :
         return false;
   }
}
private static final boolean jjCanMove_11(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 0:
         return ((jjbitVec12[i2] & l2) != 0L);
      case 39:
         return ((jjbitVec20[i2] & l2) != 0L);
      default :
         if ((jjbitVec19[i1] & l1) != 0L)
            return true;
         return false;
   }
}
private static final boolean jjCanMove_12(int hiByte, int i1, int i2, long l1, long l2)
{
   switch(hiByte)
   {
      case 39:
         return ((jjbitVec21[i2] & l2) != 0L);
      default :
         return false;
   }
}

/** Token literal values. */
public static final String[] jjstrLiteralImages = {
"", "\266", "\u27e6", "\u27e7", "\u201c", "\u201d", "\u2020", "\u2021", 
"\u2227", null, null, null, };

/** Lexer state names. */
public static final String[] lexStateNames = {
   "DEFAULT",
};
protected SimpleCharStream input_stream;
private final int[] jjrounds = new int[14];
private final int[] jjstateSet = new int[28];
protected char curChar;
/** Constructor. */
public TextTokenManager(SimpleCharStream stream){
   if (SimpleCharStream.staticFlag)
      throw new Error("ERROR: Cannot use a static CharStream class with a non-static lexical analyzer.");
   input_stream = stream;
}

/** Constructor. */
public TextTokenManager(SimpleCharStream stream, int lexState){
   this(stream);
   SwitchTo(lexState);
}

/** Reinitialise parser. */
public void ReInit(SimpleCharStream stream)
{
   jjmatchedPos = jjnewStateCnt = 0;
   curLexState = defaultLexState;
   input_stream = stream;
   ReInitRounds();
}
private void ReInitRounds()
{
   int i;
   jjround = 0x80000001;
   for (i = 14; i-- > 0;)
      jjrounds[i] = 0x80000000;
}

/** Reinitialise parser. */
public void ReInit(SimpleCharStream stream, int lexState)
{
   ReInit(stream);
   SwitchTo(lexState);
}

/** Switch to specified lex state. */
public void SwitchTo(int lexState)
{
   if (lexState >= 1 || lexState < 0)
      throw new TokenMgrError("Error: Ignoring invalid lexical state : " + lexState + ". State unchanged.", TokenMgrError.INVALID_LEXICAL_STATE);
   else
      curLexState = lexState;
}

protected Token jjFillToken()
{
   final Token t;
   final String curTokenImage;
   final int beginLine;
   final int endLine;
   final int beginColumn;
   final int endColumn;
   String im = jjstrLiteralImages[jjmatchedKind];
   curTokenImage = (im == null) ? input_stream.GetImage() : im;
   beginLine = input_stream.getBeginLine();
   beginColumn = input_stream.getBeginColumn();
   endLine = input_stream.getEndLine();
   endColumn = input_stream.getEndColumn();
   t = Token.newToken(jjmatchedKind, curTokenImage);

   t.beginLine = beginLine;
   t.endLine = endLine;
   t.beginColumn = beginColumn;
   t.endColumn = endColumn;

   return t;
}

int curLexState = 0;
int defaultLexState = 0;
int jjnewStateCnt;
int jjround;
int jjmatchedPos;
int jjmatchedKind;

/** Get the next Token. */
public Token getNextToken() 
{
  Token matchedToken;
  int curPos = 0;

  EOFLoop :
  for (;;)
  {
   try
   {
      curChar = input_stream.BeginToken();
   }
   catch(java.io.IOException e)
   {
      jjmatchedKind = 0;
      matchedToken = jjFillToken();
      return matchedToken;
   }

   jjmatchedKind = 0x7fffffff;
   jjmatchedPos = 0;
   curPos = jjMoveStringLiteralDfa0_0();
   if (jjmatchedKind != 0x7fffffff)
   {
      if (jjmatchedPos + 1 < curPos)
         input_stream.backup(curPos - jjmatchedPos - 1);
         matchedToken = jjFillToken();
         return matchedToken;
   }
   int error_line = input_stream.getEndLine();
   int error_column = input_stream.getEndColumn();
   String error_after = null;
   boolean EOFSeen = false;
   try { input_stream.readChar(); input_stream.backup(1); }
   catch (java.io.IOException e1) {
      EOFSeen = true;
      error_after = curPos <= 1 ? "" : input_stream.GetImage();
      if (curChar == '\n' || curChar == '\r') {
         error_line++;
         error_column = 0;
      }
      else
         error_column++;
   }
   if (!EOFSeen) {
      input_stream.backup(1);
      error_after = curPos <= 1 ? "" : input_stream.GetImage();
   }
   throw new TokenMgrError(EOFSeen, curLexState, error_line, error_column, error_after, curChar, TokenMgrError.LEXICAL_ERROR);
  }
}

private void jjCheckNAdd(int state)
{
   if (jjrounds[state] != jjround)
   {
      jjstateSet[jjnewStateCnt++] = state;
      jjrounds[state] = jjround;
   }
}
private void jjAddStates(int start, int end)
{
   do {
      jjstateSet[jjnewStateCnt++] = jjnextStates[start];
   } while (start++ != end);
}
private void jjCheckNAddTwoStates(int state1, int state2)
{
   jjCheckNAdd(state1);
   jjCheckNAdd(state2);
}

}
