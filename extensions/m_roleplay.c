/*
 * roleplay commands for charybdis.
 *
 * adds NPC, NPCA, and SCENE which allow users to send messages from 'fake'
 * nicknames. in the case of NPC and NPCA, the nickname will be underlined
 * to clearly show that it is fake. SCENE is a special case and not underlined.
 * these commands only work on channels set +N
 *
 * also adds oper commands FSAY and FACTION, which are like NPC and NPCA
 * except without the underline.
 *
 * all of these messages have the hostmask npc.fakeuser.invalid, and their ident
 * is the nickname of the user running the commands.
 */


#include "stdinc.h"
#include "ircd.h"
#include "client.h"
#include "modules.h"
#include "send.h"
#include "numeric.h"
#include "hash.h"
#include "s_serv.h"
#include "inline/stringops.h"
#include "chmode.h"
#include "tgchange.h"
#include "channel.h"
#include "packet.h"
#include "messages.h"

static const char roleplay_desc[] =
	"Adds a roleplaying system that allows faked nicknames to talk in a channel set +N";

static void m_scene(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_fsay(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_faction(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_npc(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_npca(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static void m_displaymsg(struct MsgBuf *msgbuf_p, struct Client *source_p, const char *channel, int underline, int action, const char *nick, const char *text);
static void me_roleplay(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[]);
static unsigned int mymode;

static int
_modinit(void)
{
	/* initalize the +N cmode */
	mymode = cflag_add('N', chm_simple);
	if (mymode == 0)
		return -1;

	return 0;
}

static void
_moddeinit(void)
{
	/* orphan the +N cmode on modunload */
	cflag_orphan('N');
}


struct Message scene_msgtab = {
	"SCENE", 0, 0, 0, 0,
	{mg_unreg, {m_scene, 3}, mg_ignore, mg_ignore, mg_ignore, {m_scene, 3}}
};

/* this serves as an alias for people who are used to inspircd/unreal m_roleplay */
struct Message ambiance_msgtab = {
	"AMBIANCE", 0, 0, 0, 0,
	{mg_unreg, {m_scene, 3}, mg_ignore, mg_ignore, mg_ignore, {m_scene, 3}}
};

struct Message fsay_msgtab = {
	"FSAY", 0, 0, 0, 0,
	{mg_unreg, {m_npc, 4}, mg_ignore, mg_ignore, mg_ignore, {m_fsay, 4}}
};

struct Message faction_msgtab = {
	"FACTION", 0, 0, 0, 0,
	{mg_unreg, {m_npca, 4}, mg_ignore, mg_ignore, mg_ignore, {m_faction, 4}}
};

struct Message npc_msgtab = {
	"NPC", 0, 0, 0, 0,
	{mg_unreg, {m_npc, 4}, mg_ignore, mg_ignore, mg_ignore, {m_npc, 4}}
};

struct Message npca_msgtab = {
	"NPCA", 0, 0, 0, 0,
	{mg_unreg, {m_npca, 4}, mg_ignore, mg_ignore, mg_ignore, {m_npca, 4}}
};

struct Message roleplay_msgtab = {
	"ROLEPLAY", 0, 0, 0, 0,
	{mg_ignore, mg_ignore, mg_ignore, mg_ignore, {me_roleplay, 4}, mg_ignore}
};

mapi_clist_av1 roleplay_clist[] = { &scene_msgtab, &ambiance_msgtab, &fsay_msgtab, &faction_msgtab, &npc_msgtab, &npca_msgtab, &roleplay_msgtab, NULL };

DECLARE_MODULE_AV2(roleplay, _modinit, _moddeinit, roleplay_clist, NULL, NULL, NULL, NULL, roleplay_desc);

static void
m_scene(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	m_displaymsg(msgbuf_p, source_p, parv[1], 0, 0, "=Scene=", parv[2]);
}

static void
m_fsay(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	m_displaymsg(msgbuf_p, source_p, parv[1], 0, 0, parv[2], parv[3]);
}

static void
m_faction(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	m_displaymsg(msgbuf_p, source_p, parv[1], 0, 1, parv[2], parv[3]);
}

static void
m_npc(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	m_displaymsg(msgbuf_p, source_p, parv[1], 1, 0, parv[2], parv[3]);
}

static void
m_npca(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	m_displaymsg(msgbuf_p, source_p, parv[1], 1, 1, parv[2], parv[3]);
}

static void
m_displaymsg(struct MsgBuf *msgbuf_p, struct Client *source_p, const char *channel, int underline, int action, const char *nick, const char *text)
{
	struct Channel *chptr;
	struct membership *msptr;
	char nick2[NICKLEN+1];
	char nick3[NICKLEN+1];
	char text3[BUFSIZE];
	char text2[BUFSIZE];

	rb_strlcpy(nick3, nick, sizeof nick3);

	if(!IsFloodDone(source_p))
		flood_endgrace(source_p);

	if((chptr = find_channel(channel)) == NULL)
	{
		sendto_one_numeric(source_p, ERR_NOSUCHCHANNEL,
				form_str(ERR_NOSUCHCHANNEL), channel);
		return;
	}

	if(!(msptr = find_channel_membership(chptr, source_p)))
	{
		sendto_one_numeric(source_p, ERR_NOTONCHANNEL,
				   form_str(ERR_NOTONCHANNEL), chptr->chname);
		return;
	}

	if(!(chptr->mode.mode & chmode_flags['N']))
	{
		sendto_one_numeric(source_p, 573, "%s :Roleplay commands are not enabled on this channel.", chptr->chname);
		return;
	}

	if(!can_send(chptr, source_p, msptr))
	{
		sendto_one_numeric(source_p, 573, "%s :Cannot send to channel.", chptr->chname);
		return;
	}

	/* enforce flood stuff on roleplay commands */
	if(flood_attack_channel(0, source_p, chptr, chptr->chname))
		return;

	/* enforce target change on roleplay commands */
	if(!is_chanop_voiced(msptr) && !IsOper(source_p) && !add_channel_target(source_p, chptr))
	{
		sendto_one(source_p, form_str(ERR_TARGCHANGE),
			   me.name, source_p->name, chptr->chname);
		return;
	}

	if(underline)
		snprintf(nick2, sizeof(nick2), "\x1F%s\x1F", strip_unprintable(nick3));
	else
		snprintf(nick2, sizeof(nick2), "%s", strip_unprintable(nick3));

	/* don't allow nicks to be empty after stripping
	 * this prevents nastiness like fake factions, etc. */
	if(EmptyString(nick3))
	{
		sendto_one_numeric(source_p, 573, "%s :No visible non-stripped characters in nick.", chptr->chname);
		return;
	}

	snprintf(text3, sizeof(text3), "%s (%s)", text, source_p->name);

	if(action)
		snprintf(text2, sizeof(text2), "\1ACTION %.500s\1", text3);
	else
		snprintf(text2, sizeof(text2), "%s", text3);

	sendto_channel_local(source_p, ALL_MEMBERS, chptr, ":%s!%s@npc.fakeuser.invalid PRIVMSG %s :%s", nick2, source_p->name, channel, text2);
	sendto_match_servs(source_p, "*", CAP_ENCAP, NOCAPS, "ENCAP * ROLEPLAY %s %s :%s",
			channel, nick2, text2);
}

static void
me_roleplay(struct MsgBuf *msgbuf_p, struct Client *client_p, struct Client *source_p, int parc, const char *parv[])
{
	struct Channel *chptr;

	/* Don't segfault if we get ROLEPLAY with an invalid channel.
	 * This shouldn't happen but it's best to be on the safe side. */
	if((chptr = find_channel(parv[1])) == NULL)
		return;

	sendto_channel_local(source_p, ALL_MEMBERS, chptr, ":%s!%s@npc.fakeuser.invalid PRIVMSG %s :%s", parv[2], source_p->name, parv[1], parv[3]);
}
