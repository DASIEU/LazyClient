#ifndef GAME_CLIENT_COMPONENTS_LAZY_LAZY_CCID_H
#define GAME_CLIENT_COMPONENTS_LAZY_LAZY_CCID_H

// Client identification through the alpha bytes of custom skin colors, as
// used by RushieClient and the DDNet custom client identification standard.
// The alpha bytes are not used for rendering, so the visible tee colors stay
// the same. Only clients that know the convention read the extra byte.
//
// BestClient does not expose a local client tag. Its icon is matched against
// BestClient's optional public player list instead.

enum ELazyClientType
{
	LC_CLIENT_NONE = 0,
	LC_CLIENT_LAZYCLIENT,
	LC_CLIENT_RUSHIE,
	LC_CLIENT_BESTCLIENT,
	LC_CLIENT_KAIZO,
	LC_CLIENT_DUCK,
	LC_CLIENT_CHILLERBOT,
	LC_CLIENT_ASCEND,
};

enum
{
	LC_CCID_BODY_LAZYCLIENT = 6,
	LC_CCID_FEET_LAZYCLIENT = 0,

	LC_CCID_BODY_KAIZO = 1,
	LC_CCID_BODY_DUCK = 2,
	LC_CCID_BODY_CHILLERBOT = 3,
	LC_CCID_BODY_RUSHIE = 4,
	LC_CCID_BODY_ASCEND = 5,

	LC_CCID_FEET_NONE = 0,
};

inline int LazyCCIDBody(int Color)
{
	return (Color >> 24) & 0xFF;
}

inline int LazyCCIDFeet(int Color)
{
	return (Color >> 24) & 0xFF;
}

inline int LazyCCIDInsert(int Color, int Id)
{
	return (Color & 0x00FFFFFF) | ((Id & 0xFF) << 24);
}

inline int LazyCCIDStrip(int Color)
{
	return Color & 0x00FFFFFF;
}

#endif
