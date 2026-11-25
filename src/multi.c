/* Volk! 
 * I was getting confused at all the different functions etc we have for 
 * multiclassed players, so I thought i'd just make a new file and put
 * everything in here. */

/* Players that reach a level where they get access to two of the same
 *  * spells/skills from different classes, will want to take the HIGHEST
 *   * adept % from those classes.
 *    * Revamped so if availabletochar is false, it will just return the
 *     * highest adept % from all classes. If TRUE, it will return the highest
 *      * within the player's level range. */
#include <time.h>
#include "h/mud.h"
#include "h/files.h"

extern bool             doubleexp;

// BS function I have to write, because of teacher code
bool IS_UNDERTWENTY(CHAR_DATA *ch, int sn)
{
  if(sn < 0 || sn >= top_sn || !IS_VALID_SN(sn))
  {
    bug("%s: invalid sn [%d]", __FUNCTION__, sn);
    return FALSE;
  }

  if(IS_THIRDCLASS(ch))
  {
    if(skill_table[sn]->skill_level[ch->thirdclass] < 21 || skill_table[sn]->skill_level[ch->secondclass] < 21 || skill_table[sn]->skill_level[ch->Class] < 21)
      return TRUE;
  }
  else if(IS_SECONDCLASS(ch))
  {
    if(skill_table[sn]->skill_level[ch->secondclass] < 21 || skill_table[sn]->skill_level[ch->Class] < 21)
      return TRUE;
  }
  else if(IS_SINGLECLASS(ch))
  {
    if(skill_table[sn]->skill_level[ch->Class] < 21)
      return TRUE;
  }
  else
  {
    return FALSE;
  }

  return FALSE;
}

/*
 * CanLearnNow: TRUE = see if they can access it now, FALSE = see if they can ever access it.
 */
bool CAN_LEARN(CHAR_DATA *ch, int sn, bool canlearnnow)
{
  if(sn < 0 || sn >= top_sn || !IS_VALID_SN(sn))
  {
    bug("%s: invalid sn [%d]", __FUNCTION__, sn);
    return FALSE;
  }

  if(!IS_NPC(ch))
  {

    if(ch->pcdata->tradeclass == 20 && (sn == gsn_mine || sn == gsn_forge))
    {
      return TRUE;
    }
    if(ch->pcdata->tradeclass == 21 && (sn == gsn_gather || sn == gsn_bake || sn == gsn_mix))
    {
      return TRUE;
    }
    if(ch->pcdata->tradeclass == 22 && (sn == gsn_hunt || sn == gsn_tan))
    {
      return TRUE;
    }
    if(ch->pcdata->tradeclass == 24 && (sn == gsn_mine || sn == gsn_jewelry))
    {
      return TRUE;
    }
    if(ch->pcdata->tradeclass == 25 && (sn == gsn_mill || sn == gsn_fell))
    {
      return TRUE;
    }
    if(ch->pcdata->tradeclass == 26 && (sn == gsn_unearth || sn == gsn_produce))
    {
      return TRUE;
    }

  }

  if(IS_THIRDCLASS(ch) && ch->race != RACE_ANIMAL)
  {
    if(ch->thirdlevel >= skill_table[sn]->skill_level[ch->thirdclass])
      return TRUE;

    if(!canlearnnow && skill_table[sn]->skill_level[ch->thirdclass] < 101 && skill_table[sn]->skill_level[ch->thirdclass] > 0)
      return TRUE;
  }

  if(IS_SECONDCLASS(ch) && ch->race != RACE_ANIMAL)
  {
    if(ch->secondlevel >= skill_table[sn]->skill_level[ch->secondclass])
      return TRUE;

    if(!canlearnnow && skill_table[sn]->skill_level[ch->secondclass] < 101 && skill_table[sn]->skill_level[ch->secondclass] > 0)
      return TRUE;

    if(ch->firstlevel >= skill_table[sn]->skill_level[ch->Class])
      return TRUE;
  }
  else if(ch->level >= skill_table[sn]->skill_level[ch->Class])
    return TRUE;

  if(!canlearnnow && skill_table[sn]->skill_level[ch->Class] < 101 && skill_table[sn]->skill_level[ch->Class] > 0)
    return TRUE;

  if(ch->race >= 0 && ch->race < MAX_RACE && skill_table[sn]->race_level[ch->race] < LEVEL_IMMORTAL)
  {
    if(ch->level >= skill_table[sn]->race_level[ch->race])
      return TRUE;

    if(!canlearnnow)
      return TRUE;
  }

  return FALSE;
}

int get_maxadept(CHAR_DATA *ch, int sn, bool availabletochar)
{
  int                     class1 = 0, class2 = 0, class3 = 0, racial = 0, result = 0;

  if(IS_NPC(ch))
    return 80;

  if(IS_IMMORTAL(ch))
    return 100;

  if(sn < 0)
  {
    // this bug has to do with the w_index in fight.c
    // bug( "player %s %s: 'sn is less than 0.", ch->name, __FUNCTION__ );
    return 100;
  }
  if(ch->pcdata->tradeclass == 20 && (sn == gsn_mine || sn == gsn_forge))
  {
    return 95;
  }
  if(ch->pcdata->tradeclass == 21 && (sn == gsn_gather || sn == gsn_bake || sn == gsn_mix))
  {
    return 95;
  }
  if(ch->pcdata->tradeclass == 22 && (sn == gsn_tan || sn == gsn_hunt))
  {
    return 95;
  }
  if(ch->pcdata->tradeclass == 24 && (sn == gsn_mine || sn == gsn_jewelry))
  {
    return 95;
  }
  if(ch->pcdata->tradeclass == 25 && (sn == gsn_mill || sn == gsn_fell))
  {
    return 95;
  }
  if(ch->pcdata->tradeclass == 26 && (sn == gsn_unearth || sn == gsn_produce))
  {
    return 95;
  }
  if(IS_THIRDCLASS(ch) && ch->race != RACE_ANIMAL)
  {
    if(skill_table[sn]->skill_level[ch->thirdclass] < 101)
    {
      class3 = skill_table[sn]->skill_adept[ch->thirdclass];
      if(availabletochar)
        if(skill_table[sn]->skill_level[ch->thirdclass] > ch->thirdlevel)
          class3 = 0; /* Means they can't use it yet, so 0% adept there. */
    }
  }

  if(IS_SECONDCLASS(ch) && ch->race != RACE_ANIMAL)
  {
    if(skill_table[sn]->skill_level[ch->secondclass] < 101)
    {
      class2 = skill_table[sn]->skill_adept[ch->secondclass];
      if(availabletochar)
        if(skill_table[sn]->skill_level[ch->secondclass] > ch->secondlevel)
          class2 = 0; /* Can't use it yet. */
    }
  }

  if(skill_table[sn]->skill_level[ch->Class] < 101)
    class1 = skill_table[sn]->skill_adept[ch->Class];

  if(availabletochar)
  {
    if(IS_SECONDCLASS(ch))
    {
      if(skill_table[sn]->skill_level[ch->Class] > ch->firstlevel)
        class1 = 0;
    }
    else
    {
      if(skill_table[sn]->skill_level[ch->Class] > ch->level)
        class1 = 0;
    }
  }

  class3 = URANGE(0, class3, sysdata.class3maxadept);
  class2 = URANGE(0, class2, sysdata.class2maxadept);
  class1 = URANGE(0, class1, sysdata.class1maxadept);

  if(ch->race >= 0 && ch->race < MAX_RACE && skill_table[sn]->race_level[ch->race] < LEVEL_IMMORTAL)
  {
    if(!availabletochar || skill_table[sn]->race_level[ch->race] <= ch->level)
      racial = URANGE(0, skill_table[sn]->race_adept[ch->race], 100);
  }

  result = class1;
  if(class2 > result)
    result = class2;
  if(class3 > result)
    result = class3;
  if(racial > result)
    result = racial;

  return result;
}

/* This will find the MAX AVAILABLE SKILL LEVEL for their classes. For example,
 *  * if player gets second attack at level 10 with one class and second
 *   * attack at level 20 with another class, and they are higher than level 20, it
 *    * will return 20. Else if higher than 10, it'll return 10. Else returns 0.
 *     * */
/* Okay, have reworked this now. Use TRUE if you want to check if it's available
 *  * to the player at their current level. Use FALSE if you just want to see what
 *   * level is the highest. :) */
// Volk: Well it should. Seems broken.

int get_maxskill(CHAR_DATA *ch, int sn, bool availabletochar)
{
  int                     sklvl1 = skill_table[sn]->skill_level[ch->Class];
  int                     sklvl2 = skill_table[sn]->skill_level[ch->secondclass];
  int                     sklvl3 = skill_table[sn]->skill_level[ch->thirdclass];
  int                     result = 0;
  bool                    one = FALSE, two = FALSE, three = FALSE;

  if(sklvl1 == 0)
    sklvl1 = 101;
  if(sklvl2 == 0)
    sklvl2 = 101;
  if(sklvl3 == 0)
    sklvl3 = 101;

/* Means players have access to said sn */
  if(availabletochar)
  {
    if(sklvl3 <= ch->thirdlevel)
      three = TRUE;
    if(sklvl2 <= ch->secondlevel)
      two = TRUE;
    if(IS_SECONDCLASS(ch))
    {
      if(sklvl1 <= ch->firstlevel)
        one = TRUE;
    }
    else
    {
      if(sklvl1 <= ch->Class)
        one = TRUE;
    }
  }
  else
  {
    three = TRUE;
    two = TRUE;
    one = TRUE;
  }

  if(three)
  {
    if(two)
    {
      if(one)
      {
        /*
         * All three are valid!! 
         */
        result = sklvl3 > sklvl2 ? UMAX(sklvl3, sklvl1) : UMAX(sklvl2, sklvl1);
      }
      else  /* Only three and two! */
        result = UMAX(sklvl3, sklvl2);
    }
    if(one) /* Might still be three and one */
      result = UMAX(sklvl3, sklvl1);
  }
  else if(two)
    result = UMAX(sklvl2, sklvl1);
  else
    result = sklvl1;

  if(ch->race >= 0 && ch->race < MAX_RACE && skill_table[sn]->race_level[ch->race] < LEVEL_IMMORTAL)
  {
    int racelvl = skill_table[sn]->race_level[ch->race];

    if(!availabletochar || racelvl <= ch->level)
    {
      if(result == 0 || result >= LEVEL_IMMORTAL)
        result = racelvl;
      else
        result = UMAX(result, racelvl);
    }
  }

  return result;
}

void advance_class_level(CHAR_DATA *ch)
{ /* ONLY called for multiclasses */
  int                     level = 0;

  if(ch->firstexp >= exp_class_level(ch, ch->firstlevel + 1, ch->Class))
  { /* Levelled */
    ch_printf(ch, "\r\n&WYou have now obtained %s level %d!&D\r\n", class_table[ch->Class]->who_name, ++ch->firstlevel);
    ch->firstexp = URANGE(0, (ch->firstexp - exp_class_level(ch, ch->firstlevel, ch->Class)), ch->firstexp);
    advance_level(ch);
  }

  if(IS_SECONDCLASS(ch) && ch->secondexp >= exp_class_level(ch, ch->secondlevel + 1, ch->secondclass))
  { /* Again */
    ch_printf(ch, "\r\n&WYou have now obtained %s level %d!&D\r\n", class_table[ch->secondclass]->who_name, ++ch->secondlevel);
    ch->secondexp = URANGE(0, (ch->secondexp - exp_class_level(ch, ch->secondlevel, ch->secondclass)), ch->secondexp);
    advance_level(ch);
  }

  if(IS_THIRDCLASS(ch) && ch->thirdexp >= exp_class_level(ch, ch->thirdlevel + 1, ch->thirdclass))
  { /* Made a level! */
    ch_printf(ch, "\r\n&WYou have now obtained %s level %d!&D\r\n", class_table[ch->thirdclass]->who_name, ++ch->thirdlevel);
    ch->thirdexp = URANGE(0, (ch->thirdexp - exp_class_level(ch, ch->thirdlevel, ch->thirdclass)), ch->thirdexp);
    advance_level(ch);
  }

  if(IS_THIRDCLASS(ch))
    level = ((ch->firstlevel + 1 + ch->secondlevel + ch->thirdlevel) / 3);
  else if(IS_SECONDCLASS(ch))
    level = ((ch->firstlevel + 1 + ch->secondlevel) / 2);
  else
    level = (ch->firstlevel + 1);

  if(level > ch->level)
  { /* Should have gained! */
    char                    buf[MSL];

    if(xIS_SET(ch->act, PLR_EXTREME))
    {
      ch_printf(ch, "&GPlaying 6D EXTREME you gain 5 glory!&D\r\n");
      ch->quest_curr += 5;
    }

    ch_printf(ch, "\r\n&WYou have now obtained an overall experience level %d!&D\r\n", ++ch->level);
    grant_racial_skills(ch, FALSE);
    restore_char(ch);
    send_to_char_color("&YYou have gained insight in the realms, and have been restored!\r\n", ch);
    snprintf(buf, MSL, "The realms rejoice as %s has just achieved level %d!&D", ch->name, ch->level);
    announce(buf);
    snprintf(buf, MSL, "%24.24s: %s obtained level %d!%s%s&D", ctime(&current_time), ch->name, ch->level, (doubleexp ? " (Double)" : ""), (happyhouron ? " (HappyHour)" : ""));
    append_to_file(PLEVEL_FILE, buf);
  }
}

void grant_racial_skills(CHAR_DATA *ch, bool silent)
{
  int                     sn;

  if(!ch || IS_NPC(ch) || !ch->pcdata || ch->race < 0 || ch->race >= MAX_RACE)
    return;

  for(sn = 0; sn < top_sn; sn++)
  {
    if(!skill_table[sn] || !skill_table[sn]->name)
      continue;

    if(skill_table[sn]->type != SKILL_RACIAL)
      continue;

    if(skill_table[sn]->race_level[ch->race] < LEVEL_IMMORTAL
       && ch->level >= skill_table[sn]->race_level[ch->race]
       && ch->pcdata->learned[sn] <= 0)
    {
      int                     adept = get_maxadept(ch, sn, TRUE);
      int                     start_pct = UMIN(25, adept > 0 ? adept : 25);

      ch->pcdata->learned[sn] = UMAX(1, start_pct);
      if(!silent)
        ch_printf(ch, "&CYour racial instincts awaken: you now know %s!&D\r\n", skill_table[sn]->name);
    }
  }
}
