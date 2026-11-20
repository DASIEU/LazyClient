#include "menus.h"

#include <base/dbg.h>
#include <base/io.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/font_icons.h>
#include <engine/gfx/image_loader.h>
#include <engine/image.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>
#include <game/mapitems.h>

#include <generated/client_data.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;

typedef std::function<void()> TMenuAssetScanLoadedFunc;

struct SMenuAssetScanUser
{
	void *m_pUser;
	TMenuAssetScanLoadedFunc m_LoadedFunc;
};

// IDs of the tabs in the Assets menu
enum
{
	ASSETS_TAB_ENTITIES = 0,
	ASSETS_TAB_GAME = 1,
	ASSETS_TAB_EMOTICONS = 2,
	ASSETS_TAB_PARTICLES = 3,
	ASSETS_TAB_HUD = 4,
	ASSETS_TAB_EXTRAS = 5,
	ASSETS_TAB_AUDIO = 6,
	ASSETS_TAB_BAKER = 7,
	NUMBER_OF_ASSETS_TABS = 8,
};

void CMenus::LoadEntities(SCustomEntities *pEntitiesItem, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pEntitiesItem->m_aName, "default") == 0)
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "editor/entities_clear/%s.png", gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
	else
	{
		for(int i = 0; i < MAP_IMAGE_MOD_TYPE_COUNT; ++i)
		{
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", pEntitiesItem->m_aName, gs_apModEntitiesNames[i]);
			pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			if(pEntitiesItem->m_aImages[i].m_Texture.IsNullTexture())
			{
				str_format(aPath, sizeof(aPath), "assets/entities/%s.png", pEntitiesItem->m_aName);
				pEntitiesItem->m_aImages[i].m_Texture = pThis->Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
			}
			if(!pEntitiesItem->m_RenderTexture.IsValid() || pEntitiesItem->m_RenderTexture.IsNullTexture())
				pEntitiesItem->m_RenderTexture = pEntitiesItem->m_aImages[i].m_Texture;
		}
	}
}

int CMenus::EntitiesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		SCustomEntities EntitiesItem;
		str_copy(EntitiesItem.m_aName, pName);
		CMenus::LoadEntities(&EntitiesItem, pUser);
		pThis->m_vEntitiesList.push_back(EntitiesItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, aName);
			CMenus::LoadEntities(&EntitiesItem, pUser);
			pThis->m_vEntitiesList.push_back(EntitiesItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

template<typename TName>
static void LoadAsset(TName *pAssetItem, const char *pAssetName, IGraphics *pGraphics)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(str_comp(pAssetItem->m_aName, "default") == 0)
	{
		str_format(aPath, sizeof(aPath), "%s.png", pAssetName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else
	{
		str_format(aPath, sizeof(aPath), "assets/%s/%s.png", pAssetName, pAssetItem->m_aName);
		pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(pAssetItem->m_RenderTexture.IsNullTexture())
		{
			str_format(aPath, sizeof(aPath), "assets/%s/%s/%s.png", pAssetName, pAssetItem->m_aName, pAssetName);
			pAssetItem->m_RenderTexture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
	}
}

template<typename TName>
static int AssetScan(const char *pName, int IsDir, int DirType, std::vector<TName> &vAssetList, const char *pAssetName, IGraphics *pGraphics, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	if(IsDir)
	{
		if(pName[0] == '.')
			return 0;

		// default is reserved
		if(str_comp(pName, "default") == 0)
			return 0;

		TName AssetItem;
		str_copy(AssetItem.m_aName, pName);
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);
	}
	else
	{
		if(str_endswith(pName, ".png"))
		{
			char aName[IO_MAX_PATH_LENGTH];
			str_truncate(aName, sizeof(aName), pName, str_length(pName) - 4);
			// default is reserved
			if(str_comp(aName, "default") == 0)
				return 0;

			TName AssetItem;
			str_copy(AssetItem.m_aName, aName);
			LoadAsset(&AssetItem, pAssetName, pGraphics);
			vAssetList.push_back(AssetItem);
		}
	}

	pRealUser->m_LoadedFunc();

	return 0;
}

int CMenus::GameScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vGameList, "game", pGraphics, pUser);
}

int CMenus::EmoticonsScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vEmoticonList, "emoticons", pGraphics, pUser);
}

int CMenus::ParticlesScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vParticlesList, "particles", pGraphics, pUser);
}

int CMenus::HudScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vHudList, "hud", pGraphics, pUser);
}

int CMenus::ExtrasScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;
	IGraphics *pGraphics = pThis->Graphics();
	return AssetScan(pName, IsDir, DirType, pThis->m_vExtrasList, "extras", pGraphics, pUser);
}

int CMenus::AudioPackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pRealUser = (SMenuAssetScanUser *)pUser;
	auto *pThis = (CMenus *)pRealUser->m_pUser;

	if(!IsDir || pName[0] == '.')
		return 0;

	// "default" means the stock sounds and is always offered explicitly.
	if(str_comp(pName, "default") == 0)
		return 0;

	for(const SCustomAudioPack &Pack : pThis->m_vAudioPackList)
	{
		if(str_comp(Pack.m_aName, pName) == 0)
			return 0;
	}

	SCustomAudioPack PackItem;
	str_copy(PackItem.m_aName, pName);
	pThis->m_vAudioPackList.push_back(PackItem);

	pRealUser->m_LoadedFunc();
	return 0;
}

static std::vector<const CMenus::SCustomEntities *> gs_vpSearchEntitiesList;
static std::vector<const CMenus::SCustomGame *> gs_vpSearchGamesList;
static std::vector<const CMenus::SCustomEmoticon *> gs_vpSearchEmoticonsList;
static std::vector<const CMenus::SCustomParticle *> gs_vpSearchParticlesList;
static std::vector<const CMenus::SCustomHud *> gs_vpSearchHudList;
static std::vector<const CMenus::SCustomExtras *> gs_vpSearchExtrasList;
static std::vector<const CMenus::SCustomAudioPack *> gs_vpSearchAudioPackList;

static bool gs_aInitCustomList[NUMBER_OF_ASSETS_TABS] = {
	true,
};

static size_t gs_aCustomListSize[NUMBER_OF_ASSETS_TABS] = {
	0,
};

static CLineInputBuffered<64> s_aFilterInputs[NUMBER_OF_ASSETS_TABS];

static int s_CurCustomTab = ASSETS_TAB_ENTITIES;

static const CMenus::SCustomItem *GetCustomItem(int CurTab, size_t Index)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
		return gs_vpSearchEntitiesList[Index];
	else if(CurTab == ASSETS_TAB_GAME)
		return gs_vpSearchGamesList[Index];
	else if(CurTab == ASSETS_TAB_EMOTICONS)
		return gs_vpSearchEmoticonsList[Index];
	else if(CurTab == ASSETS_TAB_PARTICLES)
		return gs_vpSearchParticlesList[Index];
	else if(CurTab == ASSETS_TAB_HUD)
		return gs_vpSearchHudList[Index];
	else if(CurTab == ASSETS_TAB_EXTRAS)
		return gs_vpSearchExtrasList[Index];
	else if(CurTab == ASSETS_TAB_AUDIO)
		return gs_vpSearchAudioPackList[Index];
	dbg_assert_failed("Invalid CurTab: %d", CurTab);
}

template<typename TName>
static void ClearAssetList(std::vector<TName> &vList, IGraphics *pGraphics)
{
	for(TName &Asset : vList)
	{
		pGraphics->UnloadTexture(&Asset.m_RenderTexture);
	}
	vList.clear();
}

void CMenus::ClearCustomItems(int CurTab)
{
	if(CurTab == ASSETS_TAB_ENTITIES)
	{
		for(auto &Entity : m_vEntitiesList)
		{
			for(auto &Image : Entity.m_aImages)
			{
				Graphics()->UnloadTexture(&Image.m_Texture);
			}
		}
		m_vEntitiesList.clear();

		// reload current entities
		GameClient()->m_MapImages.ChangeEntitiesPath(g_Config.m_ClAssetsEntities);
	}
	else if(CurTab == ASSETS_TAB_GAME)
	{
		ClearAssetList(m_vGameList, Graphics());

		// reload current game skin
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
	else if(CurTab == ASSETS_TAB_EMOTICONS)
	{
		ClearAssetList(m_vEmoticonList, Graphics());

		// reload current emoticons skin
		GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
	}
	else if(CurTab == ASSETS_TAB_PARTICLES)
	{
		ClearAssetList(m_vParticlesList, Graphics());

		// reload current particles skin
		GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
	}
	else if(CurTab == ASSETS_TAB_HUD)
	{
		ClearAssetList(m_vHudList, Graphics());

		// reload current hud skin
		GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
	}
	else if(CurTab == ASSETS_TAB_EXTRAS)
	{
		ClearAssetList(m_vExtrasList, Graphics());

		// reload current DDNet particles skin
		GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
	}
	else if(CurTab == ASSETS_TAB_AUDIO)
	{
		m_vAudioPackList.clear();

		// reload the currently selected sound pack
		GameClient()->m_Sounds.Clear();
	}
	else
	{
		dbg_assert_failed("Invalid CurTab: %d", CurTab);
	}
	gs_aInitCustomList[CurTab] = true;
}

template<typename TName, typename TCaller>
static void InitAssetList(std::vector<TName> &vAssetList, const char *pAssetPath, const char *pAssetName, FS_LISTDIR_CALLBACK pfnCallback, IGraphics *pGraphics, IStorage *pStorage, TCaller Caller)
{
	if(vAssetList.empty())
	{
		TName AssetItem;
		str_copy(AssetItem.m_aName, "default");
		LoadAsset(&AssetItem, pAssetName, pGraphics);
		vAssetList.push_back(AssetItem);

		// load assets
		pStorage->ListDirectory(IStorage::TYPE_ALL, pAssetPath, pfnCallback, Caller);
		std::sort(vAssetList.begin(), vAssetList.end());
	}
	if(vAssetList.size() != gs_aCustomListSize[s_CurCustomTab])
		gs_aInitCustomList[s_CurCustomTab] = true;
}

template<typename TName>
static int InitSearchList(std::vector<const TName *> &vpSearchList, std::vector<TName> &vAssetList)
{
	vpSearchList.clear();
	int ListSize = vAssetList.size();
	for(int i = 0; i < ListSize; ++i)
	{
		const TName *pAsset = &vAssetList[i];

		// filter quick search
		if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pAsset->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
			continue;

		vpSearchList.push_back(pAsset);
	}
	return vAssetList.size();
}

void CMenus::RenderSettingsAssets(CUIRect MainView)
{
	CUIRect TabBar, CustomList, QuickSearch, DirectoryButton, ReloadButton;

	MainView.HSplitTop(20.0f, &TabBar, &MainView);
	const float TabWidth = TabBar.w / (float)NUMBER_OF_ASSETS_TABS;
	static CButtonContainer s_aPageTabs[NUMBER_OF_ASSETS_TABS] = {};
	const char *apTabNames[NUMBER_OF_ASSETS_TABS] = {
		Localize("Entities"),
		Localize("Game"),
		Localize("Emoticons"),
		Localize("Particles"),
		Localize("HUD"),
		Localize("Extras"),
		Localize("Sounds"),
		Localize("Baker")};

	for(int Tab = ASSETS_TAB_ENTITIES; Tab < NUMBER_OF_ASSETS_TABS; ++Tab)
	{
		CUIRect Button;
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == ASSETS_TAB_ENTITIES ? IGraphics::CORNER_L : (Tab == NUMBER_OF_ASSETS_TABS - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
		{
			s_CurCustomTab = Tab;
		}
	}

	if(s_CurCustomTab == ASSETS_TAB_BAKER)
	{
		RenderAssetBaker(MainView);
		return;
	}

	auto LoadStartTime = time_get_nanoseconds();
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = [&]() {
		if(time_get_nanoseconds() - LoadStartTime > 500ms)
			RenderLoading(Localize("Loading assets"), "", 0);
	};
	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		if(m_vEntitiesList.empty())
		{
			SCustomEntities EntitiesItem;
			str_copy(EntitiesItem.m_aName, "default");
			LoadEntities(&EntitiesItem, &User);
			m_vEntitiesList.push_back(EntitiesItem);

			// load entities
			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/entities", EntitiesScan, &User);
			std::sort(m_vEntitiesList.begin(), m_vEntitiesList.end());
		}
		if(m_vEntitiesList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		InitAssetList(m_vGameList, "assets/game", "game", GameScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		InitAssetList(m_vEmoticonList, "assets/emoticons", "emoticons", EmoticonsScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		InitAssetList(m_vParticlesList, "assets/particles", "particles", ParticlesScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		InitAssetList(m_vHudList, "assets/hud", "hud", HudScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		InitAssetList(m_vExtrasList, "assets/extras", "extras", ExtrasScan, Graphics(), Storage(), &User);
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		if(m_vAudioPackList.empty())
		{
			SCustomAudioPack DefaultPack;
			str_copy(DefaultPack.m_aName, "default");
			m_vAudioPackList.push_back(DefaultPack);

			Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/audio", AudioPackScan, &User);
			Storage()->ListDirectory(IStorage::TYPE_ALL, "audio", AudioPackScan, &User);
			std::sort(m_vAudioPackList.begin(), m_vAudioPackList.end());
		}
		if(m_vAudioPackList.size() != gs_aCustomListSize[s_CurCustomTab])
			gs_aInitCustomList[s_CurCustomTab] = true;
	}
	else
	{
		dbg_assert_failed("Invalid s_CurCustomTab: %d", s_CurCustomTab);
	}

	MainView.HSplitTop(10.0f, nullptr, &MainView);

	// skin selector
	MainView.HSplitTop(MainView.h - 10.0f - ms_ButtonHeight, &CustomList, &MainView);
	if(gs_aInitCustomList[s_CurCustomTab])
	{
		int ListSize = 0;
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			gs_vpSearchEntitiesList.clear();
			ListSize = m_vEntitiesList.size();
			for(int i = 0; i < ListSize; ++i)
			{
				const SCustomEntities *pEntity = &m_vEntitiesList[i];

				// filter quick search
				if(!s_aFilterInputs[s_CurCustomTab].IsEmpty() && !str_utf8_find_nocase(pEntity->m_aName, s_aFilterInputs[s_CurCustomTab].GetString()))
					continue;

				gs_vpSearchEntitiesList.push_back(pEntity);
			}
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			ListSize = InitSearchList(gs_vpSearchGamesList, m_vGameList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			ListSize = InitSearchList(gs_vpSearchEmoticonsList, m_vEmoticonList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			ListSize = InitSearchList(gs_vpSearchParticlesList, m_vParticlesList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			ListSize = InitSearchList(gs_vpSearchHudList, m_vHudList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			ListSize = InitSearchList(gs_vpSearchExtrasList, m_vExtrasList);
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			ListSize = InitSearchList(gs_vpSearchAudioPackList, m_vAudioPackList);
		}
		gs_aInitCustomList[s_CurCustomTab] = false;
		gs_aCustomListSize[s_CurCustomTab] = ListSize;
	}

	int OldSelected = -1;
	float Margin = 10;
	float TextureWidth = 150;
	float TextureHeight = 150;

	size_t SearchListSize = 0;

	if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
	{
		SearchListSize = gs_vpSearchEntitiesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_GAME)
	{
		SearchListSize = gs_vpSearchGamesList.size();
		TextureHeight = 75;
	}
	else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
	{
		SearchListSize = gs_vpSearchEmoticonsList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
	{
		SearchListSize = gs_vpSearchParticlesList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_HUD)
	{
		SearchListSize = gs_vpSearchHudList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
	{
		SearchListSize = gs_vpSearchExtrasList.size();
	}
	else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
	{
		SearchListSize = gs_vpSearchAudioPackList.size();
		TextureHeight = 0;
	}

	static CListBox s_ListBox;
	s_ListBox.DoStart(TextureHeight + 15.0f + 10.0f + Margin, SearchListSize, CustomList.w / (Margin + TextureWidth), 1, OldSelected, &CustomList, false);
	for(size_t i = 0; i < SearchListSize; ++i)
	{
		const SCustomItem *pItem = GetCustomItem(s_CurCustomTab, i);
		if(pItem == nullptr)
			continue;

		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetsEntities) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetGame) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetEmoticons) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetParticles) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetHud) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
		{
			if(str_comp(pItem->m_aName, g_Config.m_ClAssetExtras) == 0)
				OldSelected = i;
		}
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
		{
			if(str_comp(pItem->m_aName, g_Config.m_SndPack) == 0)
				OldSelected = i;
		}

		const CListboxItem Item = s_ListBox.DoNextItem(pItem, OldSelected >= 0 && (size_t)OldSelected == i);
		CUIRect ItemRect = Item.m_Rect;
		ItemRect.Margin(Margin / 2, &ItemRect);
		if(!Item.m_Visible)
			continue;

		CUIRect TextureRect;
		ItemRect.HSplitTop(15, &ItemRect, &TextureRect);
		TextureRect.HSplitTop(10, nullptr, &TextureRect);
		Ui()->DoLabel(&ItemRect, pItem->m_aName, ItemRect.h - 2, TEXTALIGN_MC);
		if(pItem->m_RenderTexture.IsValid())
		{
			Graphics()->WrapClamp();
			Graphics()->TextureSet(pItem->m_RenderTexture);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1, 1, 1, 1);
			IGraphics::CQuadItem QuadItem(TextureRect.x + (TextureRect.w - TextureWidth) / 2, TextureRect.y + (TextureRect.h - TextureHeight) / 2, TextureWidth, TextureHeight);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
			Graphics()->WrapNormal();
		}
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(OldSelected != NewSelected)
	{
		if(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName[0] != '\0')
		{
			if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			{
				str_copy(g_Config.m_ClAssetsEntities, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->m_MapImages.ChangeEntitiesPath(GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
			}
			else if(s_CurCustomTab == ASSETS_TAB_GAME)
			{
				str_copy(g_Config.m_ClAssetGame, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			{
				str_copy(g_Config.m_ClAssetEmoticons, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadEmoticonsSkin(g_Config.m_ClAssetEmoticons);
			}
			else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			{
				str_copy(g_Config.m_ClAssetParticles, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadParticlesSkin(g_Config.m_ClAssetParticles);
			}
			else if(s_CurCustomTab == ASSETS_TAB_HUD)
			{
				str_copy(g_Config.m_ClAssetHud, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadHudSkin(g_Config.m_ClAssetHud);
			}
			else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			{
				str_copy(g_Config.m_ClAssetExtras, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->LoadExtrasSkin(g_Config.m_ClAssetExtras);
			}
			else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			{
				str_copy(g_Config.m_SndPack, GetCustomItem(s_CurCustomTab, NewSelected)->m_aName);
				GameClient()->m_Sounds.Clear();
			}
		}
	}

	// Quick search
	MainView.HSplitBottom(ms_ButtonHeight, &MainView, &QuickSearch);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, &DirectoryButton);
	QuickSearch.HSplitTop(5.0f, nullptr, &QuickSearch);
	if(Ui()->DoEditBox_Search(&s_aFilterInputs[s_CurCustomTab], &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive()))
	{
		gs_aInitCustomList[s_CurCustomTab] = true;
	}

	DirectoryButton.HSplitTop(5.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(175.0f, nullptr, &DirectoryButton);
	DirectoryButton.VSplitRight(25.0f, &DirectoryButton, &ReloadButton);
	DirectoryButton.VSplitRight(10.0f, &DirectoryButton, nullptr);
	static CButtonContainer s_AssetsDirId;
	if(DoButton_Menu(&s_AssetsDirId, Localize("Assets directory"), 0, &DirectoryButton))
	{
		char aBuf[IO_MAX_PATH_LENGTH];
		char aBufFull[IO_MAX_PATH_LENGTH + 7];
		if(s_CurCustomTab == ASSETS_TAB_ENTITIES)
			str_copy(aBufFull, "assets/entities");
		else if(s_CurCustomTab == ASSETS_TAB_GAME)
			str_copy(aBufFull, "assets/game");
		else if(s_CurCustomTab == ASSETS_TAB_EMOTICONS)
			str_copy(aBufFull, "assets/emoticons");
		else if(s_CurCustomTab == ASSETS_TAB_PARTICLES)
			str_copy(aBufFull, "assets/particles");
		else if(s_CurCustomTab == ASSETS_TAB_HUD)
			str_copy(aBufFull, "assets/hud");
		else if(s_CurCustomTab == ASSETS_TAB_EXTRAS)
			str_copy(aBufFull, "assets/extras");
		else if(s_CurCustomTab == ASSETS_TAB_AUDIO)
			str_copy(aBufFull, "assets/audio");
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, aBufFull, aBuf, sizeof(aBuf));
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(aBufFull, IStorage::TYPE_SAVE);
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_AssetsDirId, &DirectoryButton, Localize("Open the directory to add custom assets"));

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	static CButtonContainer s_AssetsReloadBtnId;
	if(DoButton_Menu(&s_AssetsReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadButton) || Input()->KeyPress(KEY_F5) || (Input()->KeyPress(KEY_R) && Input()->ModifierIsPressed()))
	{
		ClearCustomItems(s_CurCustomTab);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

enum
{
	BAKE_KIND_GAME = 0,
	BAKE_KIND_EMOTICONS,
	BAKE_KIND_PARTICLES,
	BAKE_KIND_HUD,
	BAKE_KIND_EXTRAS,
	BAKE_KIND_ENTITIES,
	BAKE_KIND_AUDIO,
	BAKE_KIND_COUNT,
};

struct SBakeKind
{
	const char *m_pLabel;
	const char *m_pAssetName;
	const char *m_pFolder;
	bool m_IsSheet;
	bool m_IsTiles;
};

static const SBakeKind s_aBakeKinds[BAKE_KIND_COUNT] = {
	{"Game skin", "game", "assets/game", true, false},
	{"Emoticons", "emoticons", "assets/emoticons", true, false},
	{"Particles", "particles", "assets/particles", true, false},
	{"HUD", "hud", "assets/hud", true, false},
	{"Extras", "extras", "assets/extras", true, false},
	{"Entities", nullptr, "assets/entities", false, true},
	{"Audio", nullptr, "assets/audio", false, false},
};

struct SBakeCategory
{
	const char *m_pName;
	std::vector<int> m_vSprites;
};

struct SBakePackEntry
{
	std::string m_Name;
	IGraphics::CTextureHandle m_Texture;
};

struct SBakeOverride
{
	int m_Kind = 0;
	int m_SpriteId = -1;
	std::string m_SourcePack;
};

struct SBakeFileOverride
{
	int m_Kind = 0;
	std::string m_File;
	std::string m_SourcePack;
};

static std::vector<SBakeCategory> s_avBakeCategories[BAKE_KIND_COUNT];
static bool s_aBakeCategoriesInit[BAKE_KIND_COUNT] = {};

static std::vector<SBakePackEntry> s_avBakePacks[BAKE_KIND_COUNT];
static bool s_aBakePacksLoaded[BAKE_KIND_COUNT] = {};
static std::vector<SBakeOverride> s_vBakeOverrides;
static std::vector<SBakeFileOverride> s_vBakeFileOverrides;

static int s_BakeKind = BAKE_KIND_GAME;
static int s_aBakeCategory[BAKE_KIND_COUNT] = {};
static std::string s_aBakeBasePack[BAKE_KIND_COUNT];
static bool s_BakeStateInitialized = false;
static char s_aBakeStatus[256] = "";
static CLineInputBuffered<64> s_BakeOutputName;
static CUi::SDropDownState s_BakeKindDropDown;
static CUi::SDropDownState s_aBakeBaseDropDown[BAKE_KIND_COUNT];
static CUi::SDropDownState s_aBakeSpriteDropDown[512];
// Every selection popup needs a scroll region; the popup dereferences it
// unconditionally when it is rendered.
static CScrollRegion s_BakeKindScrollRegion;
static CScrollRegion s_aBakeBaseScrollRegion[BAKE_KIND_COUNT];
static CScrollRegion s_aBakeSpriteScrollRegion[512];
static std::map<std::string, CUi::SDropDownState> s_aBakeFileDropDown[BAKE_KIND_COUNT];
static CScrollRegion s_BakeFilePopupScroll[BAKE_KIND_COUNT];
static CButtonContainer s_aBakeCategoryButtons[32];
static CScrollRegion s_BakeCategoryScroll;
static CScrollRegion s_BakeItemScroll;
static CScrollRegion s_BakeFolderScroll;

static bool BakeSpriteBelongsToImage(const CDataSprite &Sprite, const char *pImageName)
{
	return Sprite.m_pSet != nullptr && Sprite.m_pSet->m_pImage != nullptr &&
	       pImageName != nullptr && str_comp(Sprite.m_pSet->m_pImage->m_pName, pImageName) == 0;
}

static void InitBakeEntityCategories();
static bool FindBakeEntitiesPreviewImage(const char *pPack, char *pBuffer, int BufferSize, IStorage *pStorage);

static const std::vector<SBakeCategory> &GetBakeCategories(int Kind)
{
	if(!s_aBakeCategoriesInit[Kind] && g_pData != nullptr)
	{
		s_aBakeCategoriesInit[Kind] = true;
		std::vector<SBakeCategory> &vCategories = s_avBakeCategories[Kind];
		vCategories.clear();

		if(Kind == BAKE_KIND_ENTITIES)
		{
			InitBakeEntityCategories();
		}
		else if(Kind == BAKE_KIND_GAME)
		{
			vCategories = {
				{"Weapons", {SPRITE_WEAPON_HAMMER_BODY, SPRITE_WEAPON_HAMMER_CURSOR, SPRITE_WEAPON_GUN_BODY, SPRITE_WEAPON_GUN_CURSOR, SPRITE_WEAPON_GUN_PROJ, SPRITE_WEAPON_GUN_MUZZLE1, SPRITE_WEAPON_GUN_MUZZLE2, SPRITE_WEAPON_GUN_MUZZLE3, SPRITE_WEAPON_SHOTGUN_BODY, SPRITE_WEAPON_SHOTGUN_CURSOR, SPRITE_WEAPON_SHOTGUN_PROJ, SPRITE_WEAPON_SHOTGUN_MUZZLE1, SPRITE_WEAPON_SHOTGUN_MUZZLE2, SPRITE_WEAPON_SHOTGUN_MUZZLE3, SPRITE_WEAPON_GRENADE_BODY, SPRITE_WEAPON_GRENADE_CURSOR, SPRITE_WEAPON_GRENADE_PROJ, SPRITE_WEAPON_LASER_BODY, SPRITE_WEAPON_LASER_CURSOR, SPRITE_WEAPON_LASER_PROJ, SPRITE_WEAPON_NINJA_BODY, SPRITE_WEAPON_NINJA_CURSOR, SPRITE_WEAPON_NINJA_MUZZLE1, SPRITE_WEAPON_NINJA_MUZZLE2, SPRITE_WEAPON_NINJA_MUZZLE3}},
				{"Pickups", {SPRITE_PICKUP_HEALTH, SPRITE_PICKUP_ARMOR, SPRITE_PICKUP_HAMMER, SPRITE_PICKUP_GUN, SPRITE_PICKUP_SHOTGUN, SPRITE_PICKUP_GRENADE, SPRITE_PICKUP_LASER, SPRITE_PICKUP_NINJA, SPRITE_PICKUP_ARMOR_SHOTGUN, SPRITE_PICKUP_ARMOR_GRENADE, SPRITE_PICKUP_ARMOR_NINJA, SPRITE_PICKUP_ARMOR_LASER, SPRITE_PICKUP_FREEZE}},
				{"Health & armor", {SPRITE_HEALTH_FULL, SPRITE_HEALTH_EMPTY, SPRITE_ARMOR_FULL, SPRITE_ARMOR_EMPTY}},
				{"Hook", {SPRITE_HOOK_CHAIN, SPRITE_HOOK_HEAD}},
				{"Flags", {SPRITE_FLAG_BLUE, SPRITE_FLAG_RED}},
				{"Particles & stars", {SPRITE_PART1, SPRITE_PART2, SPRITE_PART3, SPRITE_PART4, SPRITE_PART5, SPRITE_PART6, SPRITE_PART7, SPRITE_PART8, SPRITE_PART9, SPRITE_STAR1, SPRITE_STAR2, SPRITE_STAR3}},
			};

			// Also expose any game sprite that is not in one of the curated groups,
			// so the baker stays complete when sprites are added.
			std::vector<bool> vUsed(g_pData->m_NumSprites, false);
			for(const SBakeCategory &Category : vCategories)
			{
				for(int SpriteId : Category.m_vSprites)
				{
					if(SpriteId >= 0 && SpriteId < g_pData->m_NumSprites)
						vUsed[SpriteId] = true;
				}
			}
			SBakeCategory Other;
			Other.m_pName = "Other game sprites";
			for(int i = 0; i < g_pData->m_NumSprites; i++)
			{
				if(BakeSpriteBelongsToImage(g_pData->m_aSprites[i], "game") && !vUsed[i])
					Other.m_vSprites.push_back(i);
			}
			if(!Other.m_vSprites.empty())
				vCategories.push_back(Other);
		}
		else
		{
			SBakeCategory All;
			All.m_pName = "All sprites";
			for(int i = 0; i < g_pData->m_NumSprites; i++)
			{
				if(BakeSpriteBelongsToImage(g_pData->m_aSprites[i], s_aBakeKinds[Kind].m_pAssetName))
					All.m_vSprites.push_back(i);
			}
			vCategories.push_back(All);
		}
	}
	return s_avBakeCategories[Kind];
}

struct SBakePackScanUser
{
	std::vector<std::string> *m_pNames;
	bool m_AllowFiles;
};

static int BakePackScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pUserData = static_cast<SBakePackScanUser *>(pUser);
	if(pName[0] == '.')
		return 0;
	if(!IsDir && (!pUserData->m_AllowFiles || str_endswith_nocase(pName, ".png") == nullptr))
		return 0;

	std::string Name = pName;
	if(!IsDir)
		Name.resize(Name.size() - 4);
	for(const std::string &Existing : *pUserData->m_pNames)
	{
		if(Existing == Name)
			return 0;
	}
	pUserData->m_pNames->push_back(Name);
	return 0;
}

static bool FindBakeSheetPath(int Kind, const char *pPack, char *pBuffer, int BufferSize, IStorage *pStorage)
{
	const SBakeKind &KindInfo = s_aBakeKinds[Kind];
	if(str_comp(pPack, "default") == 0)
	{
		str_format(pBuffer, BufferSize, "%s.png", KindInfo.m_pAssetName);
		return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
	}

	str_format(pBuffer, BufferSize, "%s/%s.png", KindInfo.m_pFolder, pPack);
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return true;

	str_format(pBuffer, BufferSize, "%s/%s/%s.png", KindInfo.m_pFolder, pPack, KindInfo.m_pAssetName);
	return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
}

static void BakeUnloadPacks(IGraphics *pGraphics)
{
	for(int Kind = 0; Kind < BAKE_KIND_COUNT; Kind++)
	{
		for(SBakePackEntry &Pack : s_avBakePacks[Kind])
		{
			if(Pack.m_Texture.IsValid())
				pGraphics->UnloadTexture(&Pack.m_Texture);
		}
		s_avBakePacks[Kind].clear();
		s_aBakePacksLoaded[Kind] = false;
		s_aBakeCategoriesInit[Kind] = false;
		s_avBakeCategories[Kind].clear();
	}
}

static void BakeLoadPacks(int Kind, IGraphics *pGraphics, IStorage *pStorage)
{
	if(s_aBakePacksLoaded[Kind])
		return;
	s_aBakePacksLoaded[Kind] = true;

	const SBakeKind &KindInfo = s_aBakeKinds[Kind];
	std::vector<SBakePackEntry> &vPacks = s_avBakePacks[Kind];
	vPacks.clear();

	SBakePackEntry Default;
	Default.m_Name = "default";
	if(KindInfo.m_IsSheet)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		if(FindBakeSheetPath(Kind, "default", aPath, sizeof(aPath), pStorage))
			Default.m_Texture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	else if(KindInfo.m_IsTiles)
	{
		char aPath[IO_MAX_PATH_LENGTH];
		if(FindBakeEntitiesPreviewImage("default", aPath, sizeof(aPath), pStorage))
			Default.m_Texture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
	}
	vPacks.push_back(std::move(Default));

	std::vector<std::string> vNames;
	// Sheet packs can be either folders or plain PNGs. Entities additionally
	// accept a single PNG. Audio packs are always folders.
	SBakePackScanUser User{&vNames, Kind != BAKE_KIND_AUDIO};
	pStorage->ListDirectory(IStorage::TYPE_ALL, KindInfo.m_pFolder, BakePackScan, &User);
	if(Kind == BAKE_KIND_AUDIO)
	{
		// Stock audio packs may live in the regular data "audio" folder too.
		pStorage->ListDirectory(IStorage::TYPE_ALL, "audio", BakePackScan, &User);
	}
	std::sort(vNames.begin(), vNames.end());

	for(const std::string &Name : vNames)
	{
		// The synthetic "default" pack is always added above.
		if(Name == "default")
			continue;

		SBakePackEntry Pack;
		Pack.m_Name = Name;
		if(KindInfo.m_IsSheet)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			if(FindBakeSheetPath(Kind, Name.c_str(), aPath, sizeof(aPath), pStorage))
				Pack.m_Texture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
		else if(KindInfo.m_IsTiles)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			if(FindBakeEntitiesPreviewImage(Name.c_str(), aPath, sizeof(aPath), pStorage))
				Pack.m_Texture = pGraphics->LoadTexture(aPath, IStorage::TYPE_ALL);
		}
		vPacks.push_back(std::move(Pack));
	}
}

static int BakeFindPackIndex(int Kind, const char *pName)
{
	const std::vector<SBakePackEntry> &vPacks = s_avBakePacks[Kind];
	for(size_t i = 0; i < vPacks.size(); i++)
	{
		if(str_comp(vPacks[i].m_Name.c_str(), pName) == 0)
			return (int)i;
	}
	return -1;
}

static int BakeFindOverride(int Kind, int SpriteId)
{
	for(size_t i = 0; i < s_vBakeOverrides.size(); i++)
	{
		if(s_vBakeOverrides[i].m_Kind == Kind && s_vBakeOverrides[i].m_SpriteId == SpriteId)
			return (int)i;
	}
	return -1;
}

static int BakeFindFileOverride(int Kind, const char *pFile)
{
	for(size_t i = 0; i < s_vBakeFileOverrides.size(); i++)
	{
		if(s_vBakeFileOverrides[i].m_Kind == Kind && s_vBakeFileOverrides[i].m_File == pFile)
			return (int)i;
	}
	return -1;
}

// Entities packs are either a folder with one PNG per mod or a single PNG
// that replaces the ddnet image.
static const char *const s_apBakeEntityMods[] = {
	"ddnet", "ddrace", "race", "blockworlds", "fng", "vanilla", "f-ddrace"};
static constexpr int BAKE_ENTITY_MOD_COUNT = (int)(sizeof(s_apBakeEntityMods) / sizeof(s_apBakeEntityMods[0]));
static constexpr int BAKE_ENTITY_TILE_COUNT = 256;

static bool BakeEntitiesBaseIsFolder(const char *pPack, IStorage *pStorage)
{
	if(str_comp(pPack, "default") == 0)
		return true;
	char aPath[IO_MAX_PATH_LENGTH];
	str_format(aPath, sizeof(aPath), "assets/entities/%s", pPack);
	return pStorage->FolderExists(aPath, IStorage::TYPE_ALL);
}

// Resolves the image that contains the given mod for an entities pack. Single
// PNG packs are treated as a replacement for every mod because that is the
// only usable way to source tiles from them.
static bool FindBakeEntitiesImage(const char *pPack, const char *pMod, char *pBuffer, int BufferSize, IStorage *pStorage)
{
	if(str_comp(pPack, "default") == 0)
	{
		str_format(pBuffer, BufferSize, "editor/entities_clear/%s.png", pMod);
		return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
	}

	str_format(pBuffer, BufferSize, "assets/entities/%s/%s.png", pPack, pMod);
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return true;

	str_format(pBuffer, BufferSize, "assets/entities/%s/%s.png", pPack, "ddnet");
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return true;

	str_format(pBuffer, BufferSize, "assets/entities/%s.png", pPack);
	return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
}

static bool FindBakeEntitiesPreviewImage(const char *pPack, char *pBuffer, int BufferSize, IStorage *pStorage)
{
	for(const char *pMod : s_apBakeEntityMods)
	{
		if(FindBakeEntitiesImage(pPack, pMod, pBuffer, BufferSize, pStorage))
			return true;
	}
	return false;
}

static int BakeEntityTileFromId(int EntityId)
{
	return ENTITY_OFFSET + EntityId;
}

static const char *BakeEntityTileName(int Tile)
{
	switch(Tile)
	{
	case TILE_AIR: return "Air";
	case TILE_SOLID: return "Hookable";
	case TILE_DEATH: return "Death";
	case TILE_NOHOOK: return "Unhookable";
	case TILE_NOLASER: return "No laser";
	case TILE_THROUGH_CUT: return "Hookthrough cut";
	case TILE_THROUGH: return "Hookthrough";
	case TILE_JUMP: return "Jump";
	case TILE_FREEZE: return "Freeze";
	case TILE_TELEINEVIL: return "Teleport in (evil)";
	case TILE_UNFREEZE: return "Unfreeze";
	case TILE_DFREEZE: return "Deep freeze";
	case TILE_DUNFREEZE: return "Deep unfreeze";
	case TILE_TELEINWEAPON: return "Teleport in (weapon)";
	case TILE_TELEINHOOK: return "Teleport in (hook)";
	case TILE_WALLJUMP: return "Wall jump";
	case TILE_EHOOK_ENABLE: return "E-Hook enable";
	case TILE_EHOOK_DISABLE: return "E-Hook disable";
	case TILE_HIT_ENABLE: return "Hit enable";
	case TILE_HIT_DISABLE: return "Hit disable";
	case TILE_SOLO_ENABLE: return "Solo enable";
	case TILE_SWITCHTIMEDOPEN: return "Timed switch open";
	case TILE_SWITCHTIMEDCLOSE: return "Timed switch close";
	case TILE_SWITCHOPEN: return "Switch open";
	case TILE_SWITCHCLOSE: return "Switch close";
	case TILE_TELEIN: return "Teleport in";
	case TILE_TELEOUT: return "Teleport out";
	case TILE_SPEED_BOOST_OLD: return "Speed boost old";
	case TILE_SPEED_BOOST: return "Speed boost";
	case TILE_TELECHECKOUT: return "Check-tele out";
	case TILE_TELECHECKIN: return "Check-tele in";
	case TILE_REFILL_JUMPS: return "Refill jumps";
	case TILE_START: return "Start";
	case TILE_FINISH: return "Finish";
	case TILE_STOP: return "Stop";
	case TILE_STOPS: return "Stop (special)";
	case TILE_STOPA: return "Stop (all)";
	case TILE_TELECHECKINEVIL: return "Check-tele in (evil)";
	case TILE_CP: return "Checkpoint";
	case TILE_CP_F: return "Finish checkpoint";
	case TILE_THROUGH_ALL: return "Hookthrough all";
	case TILE_THROUGH_DIR: return "Hookthrough dir";
	case TILE_TUNE: return "Tune";
	case TILE_OLDLASER: return "Old laser";
	case TILE_NPC: return "NPC";
	case TILE_EHOOK: return "E-Hook";
	case TILE_NOHIT: return "No hit";
	case TILE_NPH: return "NPH";
	case TILE_UNLOCK_TEAM: return "Unlock team";
	case TILE_ADD_TIME: return "Add time";
	case TILE_NPC_DISABLE: return "NPC disable";
	case TILE_UNLIMITED_JUMPS_DISABLE: return "Unlimited jumps disable";
	case TILE_JETPACK_DISABLE: return "Jetpack disable";
	case TILE_NPH_DISABLE: return "NPH disable";
	case TILE_SUBTRACT_TIME: return "Subtract time";
	case TILE_TELE_GUN_ENABLE: return "Tele gun enable";
	case TILE_TELE_GUN_DISABLE: return "Tele gun disable";
	case TILE_ALLOW_TELE_GUN: return "Allow tele gun";
	case TILE_ALLOW_BLUE_TELE_GUN: return "Allow blue tele gun";
	case TILE_NPC_ENABLE: return "NPC enable";
	case TILE_UNLIMITED_JUMPS_ENABLE: return "Unlimited jumps enable";
	case TILE_JETPACK_ENABLE: return "Jetpack enable";
	case TILE_NPH_ENABLE: return "NPH enable";
	case TILE_TELE_GRENADE_ENABLE: return "Tele grenade enable";
	case TILE_TELE_GRENADE_DISABLE: return "Tele grenade disable";
	case TILE_TELE_LASER_ENABLE: return "Tele laser enable";
	case TILE_TELE_LASER_DISABLE: return "Tele laser disable";
	case TILE_CREDITS_1: return "Credits/font 1";
	case TILE_CREDITS_2: return "Credits/font 2";
	case TILE_CREDITS_3: return "Credits/font 3";
	case TILE_CREDITS_4: return "Credits/font 4";
	case TILE_LFREEZE: return "Live freeze";
	case TILE_LUNFREEZE: return "Live unfreeze";
	case TILE_CREDITS_5: return "Credits/font 5";
	case TILE_CREDITS_6: return "Credits/font 6";
	case TILE_CREDITS_7: return "Credits/font 7";
	case TILE_CREDITS_8: return "Credits/font 8";
	case TILE_ENTITIES_OFF_1: return "Entities off 1";
	case TILE_ENTITIES_OFF_2: return "Entities off 2";
	default: break;
	}

	if(Tile > ENTITY_OFFSET && Tile < BAKE_ENTITY_TILE_COUNT)
	{
		switch(Tile - ENTITY_OFFSET)
		{
		case ENTITY_SPAWN: return "Spawn";
		case ENTITY_SPAWN_RED: return "Spawn red";
		case ENTITY_SPAWN_BLUE: return "Spawn blue";
		case ENTITY_FLAGSTAND_RED: return "Flag red";
		case ENTITY_FLAGSTAND_BLUE: return "Flag blue";
		case ENTITY_ARMOR_1: return "Armor";
		case ENTITY_HEALTH_1: return "Health";
		case ENTITY_WEAPON_SHOTGUN: return "Shotgun";
		case ENTITY_WEAPON_GRENADE: return "Grenade";
		case ENTITY_POWERUP_NINJA: return "Ninja";
		case ENTITY_WEAPON_LASER: return "Laser";
		case ENTITY_LASER_FAST_CCW: return "Laser fast (ccw)";
		case ENTITY_LASER_NORMAL_CCW: return "Laser normal (ccw)";
		case ENTITY_LASER_SLOW_CCW: return "Laser slow (ccw)";
		case ENTITY_LASER_STOP: return "Laser stop";
		case ENTITY_LASER_SLOW_CW: return "Laser slow (cw)";
		case ENTITY_LASER_NORMAL_CW: return "Laser normal (cw)";
		case ENTITY_LASER_FAST_CW: return "Laser fast (cw)";
		case ENTITY_ARMOR_SHOTGUN: return "Armor shotgun";
		case ENTITY_ARMOR_GRENADE: return "Armor grenade";
		case ENTITY_ARMOR_NINJA: return "Armor ninja";
		case ENTITY_ARMOR_LASER: return "Armor laser";
		case ENTITY_DRAGGER_WEAK: return "Dragger weak";
		case ENTITY_DRAGGER_NORMAL: return "Dragger normal";
		case ENTITY_DRAGGER_STRONG: return "Dragger strong";
		case ENTITY_DRAGGER_WEAK_NW: return "Dragger weak (no wall)";
		case ENTITY_DRAGGER_NORMAL_NW: return "Dragger normal (no wall)";
		case ENTITY_DRAGGER_STRONG_NW: return "Dragger strong (no wall)";
		case ENTITY_DOOR: return "Door";
		default: break;
		}
	}

	return nullptr;
}

static void InitBakeEntityCategories()
{
	std::vector<SBakeCategory> &vCategories = s_avBakeCategories[BAKE_KIND_ENTITIES];
	vCategories.clear();

	vCategories.push_back({"Solid & hookable", {TILE_SOLID}});
	vCategories.push_back({"Unhookable", {TILE_NOHOOK}});
	vCategories.push_back({"Death & freeze", {TILE_DEATH, TILE_FREEZE, TILE_UNFREEZE, TILE_DFREEZE, TILE_DUNFREEZE, TILE_LFREEZE, TILE_LUNFREEZE}});
	vCategories.push_back({"Hookthrough", {TILE_THROUGH_CUT, TILE_THROUGH, TILE_THROUGH_ALL, TILE_THROUGH_DIR}});
	vCategories.push_back({"Teleporters", {TILE_TELEINEVIL, TILE_TELEINWEAPON, TILE_TELEINHOOK, TILE_TELEIN, TILE_TELEOUT, TILE_TELECHECKOUT, TILE_TELECHECKIN, TILE_TELECHECKINEVIL, TILE_TELE_GUN_ENABLE, TILE_TELE_GUN_DISABLE, TILE_ALLOW_TELE_GUN, TILE_ALLOW_BLUE_TELE_GUN, TILE_TELE_GRENADE_ENABLE, TILE_TELE_GRENADE_DISABLE, TILE_TELE_LASER_ENABLE, TILE_TELE_LASER_DISABLE}});
	vCategories.push_back({"Switches & doors", {TILE_SWITCHTIMEDOPEN, TILE_SWITCHTIMEDCLOSE, TILE_SWITCHOPEN, TILE_SWITCHCLOSE}});

	SBakeCategory Race;
	Race.m_pName = "Race & checkpoints";
	Race.m_vSprites = {TILE_SPEED_BOOST_OLD, TILE_SPEED_BOOST, TILE_REFILL_JUMPS, TILE_START, TILE_FINISH, TILE_STOP, TILE_STOPS, TILE_STOPA, TILE_CP, TILE_CP_F, TILE_ADD_TIME, TILE_SUBTRACT_TIME};
	for(int Tile = TILE_TIME_CHECKPOINT_FIRST; Tile <= TILE_TIME_CHECKPOINT_LAST; ++Tile)
		Race.m_vSprites.push_back(Tile);
	vCategories.push_back(Race);

	vCategories.push_back({"Game tiles", {TILE_JUMP, TILE_WALLJUMP, TILE_EHOOK_ENABLE, TILE_EHOOK_DISABLE, TILE_HIT_ENABLE, TILE_HIT_DISABLE, TILE_SOLO_ENABLE, TILE_TUNE, TILE_OLDLASER, TILE_NPC, TILE_EHOOK, TILE_NOHIT, TILE_NPH, TILE_UNLOCK_TEAM, TILE_NPC_DISABLE, TILE_UNLIMITED_JUMPS_DISABLE, TILE_JETPACK_DISABLE, TILE_NPH_DISABLE, TILE_NPC_ENABLE, TILE_UNLIMITED_JUMPS_ENABLE, TILE_JETPACK_ENABLE, TILE_NPH_ENABLE}});
	vCategories.push_back({"Credits & font", {TILE_CREDITS_1, TILE_CREDITS_2, TILE_CREDITS_3, TILE_CREDITS_4, TILE_CREDITS_5, TILE_CREDITS_6, TILE_CREDITS_7, TILE_CREDITS_8}});

	SBakeCategory Entities;
	Entities.m_pName = "Entities";
	Entities.m_vSprites.push_back(TILE_ENTITIES_OFF_1);
	Entities.m_vSprites.push_back(TILE_ENTITIES_OFF_2);
	for(int Tile = BakeEntityTileFromId(ENTITY_SPAWN); Tile < BAKE_ENTITY_TILE_COUNT; ++Tile)
		Entities.m_vSprites.push_back(Tile);
	vCategories.push_back(Entities);

	std::vector<bool> vUsed(BAKE_ENTITY_TILE_COUNT, false);
	for(const SBakeCategory &Category : vCategories)
	{
		for(int Tile : Category.m_vSprites)
		{
			if(Tile > 0 && Tile < BAKE_ENTITY_TILE_COUNT)
				vUsed[Tile] = true;
		}
	}
	SBakeCategory Other;
	Other.m_pName = "Other tiles";
	for(int Tile = 1; Tile < BAKE_ENTITY_TILE_COUNT; ++Tile)
	{
		if(!vUsed[Tile])
			Other.m_vSprites.push_back(Tile);
	}
	if(!Other.m_vSprites.empty())
		vCategories.push_back(Other);
}


static bool LoadBakeSheetImage(int Kind, const char *pPack, CImageInfo &Image, IStorage *pStorage)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!FindBakeSheetPath(Kind, pPack, aPath, sizeof(aPath), pStorage))
		return false;

	IOHANDLE File = pStorage->OpenFile(aPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return false;

	int PngliteIncompatible = 0;
	return CImageLoader::LoadPng(File, aPath, Image, PngliteIncompatible);
}

static void CopyToRgba(const CImageInfo &Src, CImageInfo &Dst)
{
	Dst.m_Width = Src.m_Width;
	Dst.m_Height = Src.m_Height;
	Dst.m_Format = CImageInfo::FORMAT_RGBA;
	Dst.Allocate();
	if(Src.m_Format == CImageInfo::FORMAT_RGBA)
	{
		mem_copy(Dst.m_pData, Src.m_pData, Src.DataSize());
		return;
	}
	for(size_t y = 0; y < Src.m_Height; ++y)
	{
		for(size_t x = 0; x < Src.m_Width; ++x)
			Dst.SetPixelColor(x, y, Src.PixelColor(x, y));
	}
}

static void GetBakeSpriteRect(const CImageInfo &Image, const CDataSprite &Sprite, int &X, int &Y, int &W, int &H)
{
	const int GridX = std::max(1, Sprite.m_pSet->m_Gridx);
	const int GridY = std::max(1, Sprite.m_pSet->m_Gridy);
	X = (int)((int64_t)Sprite.m_X * (int64_t)Image.m_Width / GridX);
	Y = (int)((int64_t)Sprite.m_Y * (int64_t)Image.m_Height / GridY);
	W = (int)((int64_t)Sprite.m_W * (int64_t)Image.m_Width / GridX);
	H = (int)((int64_t)Sprite.m_H * (int64_t)Image.m_Height / GridY);
	X = std::clamp(X, 0, (int)Image.m_Width);
	Y = std::clamp(Y, 0, (int)Image.m_Height);
	W = std::clamp(W, 0, (int)Image.m_Width - X);
	H = std::clamp(H, 0, (int)Image.m_Height - Y);
}

static void BlitBakeSprite(CImageInfo &Dst, const CImageInfo &Src, const CDataSprite &Sprite)
{
	int SrcX, SrcY, SrcW, SrcH;
	int DstX, DstY, DstW, DstH;
	GetBakeSpriteRect(Src, Sprite, SrcX, SrcY, SrcW, SrcH);
	GetBakeSpriteRect(Dst, Sprite, DstX, DstY, DstW, DstH);
	if(SrcW <= 0 || SrcH <= 0 || DstW <= 0 || DstH <= 0)
		return;

	if(Src.m_Format == Dst.m_Format)
	{
		const size_t SrcPixel = Src.PixelSize();
		const size_t DstPixel = Dst.PixelSize();
		for(int y = 0; y < DstH; ++y)
		{
			const int Sy = SrcY + (int)((int64_t)y * SrcH / DstH);
			for(int x = 0; x < DstW; ++x)
			{
				const int Sx = SrcX + (int)((int64_t)x * SrcW / DstW);
				const size_t SrcOffset = ((size_t)Sy * Src.m_Width + Sx) * SrcPixel;
				const size_t DstOffset = ((size_t)(DstY + y) * Dst.m_Width + (DstX + x)) * DstPixel;
				mem_copy(&Dst.m_pData[DstOffset], &Src.m_pData[SrcOffset], std::min(SrcPixel, DstPixel));
			}
		}
	}
	else
	{
		for(int y = 0; y < DstH; ++y)
		{
			const int Sy = SrcY + (int)((int64_t)y * SrcH / DstH);
			for(int x = 0; x < DstW; ++x)
			{
				const int Sx = SrcX + (int)((int64_t)x * SrcW / DstW);
				Dst.SetPixelColor(DstX + x, DstY + y, Src.PixelColor(Sx, Sy));
			}
		}
	}
}

static void BlitBakeTile(CImageInfo &Dst, const CImageInfo &Src, int Tile)
{
	CDataSpriteset Set{};
	Set.m_Gridx = 16;
	Set.m_Gridy = 16;
	CDataSprite Sprite{};
	Sprite.m_pSet = &Set;
	Sprite.m_X = Tile % 16;
	Sprite.m_Y = Tile / 16;
	Sprite.m_W = 1;
	Sprite.m_H = 1;
	BlitBakeSprite(Dst, Src, Sprite);
}

static void DrawBakePreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const std::vector<int> &vSprites, CUIRect Area)
{
	if(!Texture.IsValid() || Texture.IsNullTexture())
		return;

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	if(vSprites.empty())
	{
		float DrawW = Area.w;
		float DrawH = Area.w / 2.0f;
		if(DrawH > Area.h)
		{
			DrawH = Area.h;
			DrawW = Area.h * 2.0f;
		}
		IGraphics::CQuadItem Quad(Area.x + (Area.w - DrawW) / 2.0f, Area.y + (Area.h - DrawH) / 2.0f, DrawW, DrawH);
		pGraphics->QuadsSetSubsetFree(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
		pGraphics->QuadsDrawTL(&Quad, 1);
	}
	else
	{
		const int Columns = std::min<int>(4, (int)vSprites.size());
		const int Rows = ((int)vSprites.size() + Columns - 1) / Columns;
		const float CellW = Area.w / Columns;
		const float CellH = Area.h / Rows;
		for(size_t i = 0; i < vSprites.size(); ++i)
		{
			const CDataSprite &Sprite = g_pData->m_aSprites[vSprites[i]];
			if(Sprite.m_W <= 0 || Sprite.m_H <= 0 || Sprite.m_pSet->m_Gridx <= 0 || Sprite.m_pSet->m_Gridy <= 0)
				continue;

			const float Aspect = ((float)Sprite.m_W / Sprite.m_pSet->m_Gridx) / ((float)Sprite.m_H / Sprite.m_pSet->m_Gridy);
			float DrawW = CellW * 0.82f;
			float DrawH = CellH * 0.82f;
			if(DrawW / DrawH > Aspect)
				DrawW = DrawH * Aspect;
			else
				DrawH = DrawW / Aspect;
			const float X = Area.x + (i % Columns) * CellW + (CellW - DrawW) / 2.0f;
			const float Y = Area.y + (i / Columns) * CellH + (CellH - DrawH) / 2.0f;

			const float U0 = (float)Sprite.m_X / Sprite.m_pSet->m_Gridx;
			const float V0 = (float)Sprite.m_Y / Sprite.m_pSet->m_Gridy;
			const float U1 = (float)(Sprite.m_X + Sprite.m_W) / Sprite.m_pSet->m_Gridx;
			const float V1 = (float)(Sprite.m_Y + Sprite.m_H) / Sprite.m_pSet->m_Gridy;

			IGraphics::CQuadItem Quad(X, Y, DrawW, DrawH);
			pGraphics->QuadsSetSubsetFree(U0, V0, U1, V0, U1, V1, U0, V1);
			pGraphics->QuadsDrawTL(&Quad, 1);
		}
	}

	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

static void DrawBakeTilePreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, int Tile, CUIRect Area)
{
	if(!Texture.IsValid() || Texture.IsNullTexture() || Tile < 0 || Tile >= BAKE_ENTITY_TILE_COUNT)
		return;

	const float Size = std::min(Area.w, Area.h);
	const float X = Area.x + (Area.w - Size) / 2.0f;
	const float Y = Area.y + (Area.h - Size) / 2.0f;

	const float U0 = (float)(Tile % 16) / 16.0f;
	const float V0 = (float)(Tile / 16) / 16.0f;
	const float U1 = U0 + 1.0f / 16.0f;
	const float V1 = V0 + 1.0f / 16.0f;

	pGraphics->WrapClamp();
	pGraphics->TextureSet(Texture);
	pGraphics->QuadsBegin();
	pGraphics->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	IGraphics::CQuadItem Quad(X, Y, Size, Size);
	pGraphics->QuadsSetSubsetFree(U0, V0, U1, V0, U1, V1, U0, V1);
	pGraphics->QuadsDrawTL(&Quad, 1);
	pGraphics->QuadsEnd();
	pGraphics->WrapNormal();
}

static bool CopyBakeFile(IStorage *pStorage, const char *pSrc, const char *pDst);

static void CreateBakeParentFolders(IStorage *pStorage, const char *pPath)
{
	char aDir[IO_MAX_PATH_LENGTH];
	str_copy(aDir, pPath, sizeof(aDir));
	char *pSlash = (char *)str_rchr(aDir, '/');
	if(pSlash == nullptr)
		return;
	*pSlash = '\0';
	for(char *p = aDir + 1; *p != '\0'; ++p)
	{
		if(*p == '/')
		{
			*p = '\0';
			pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);
			*p = '/';
		}
	}
	pStorage->CreateFolder(aDir, IStorage::TYPE_SAVE);
}

struct SBakeDirScanUser
{
	std::vector<std::string> *m_pFiles;
	std::vector<std::string> *m_pDirs;
};

static int BakeDirScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pUserData = static_cast<SBakeDirScanUser *>(pUser);
	if(pName[0] == '.')
		return 0;
	if(IsDir)
		pUserData->m_pDirs->emplace_back(pName);
	else
		pUserData->m_pFiles->emplace_back(pName);
	return 0;
}

static void CollectBakeFiles(IStorage *pStorage, const char *pDir, const std::string &Prefix, std::vector<std::string> &vOut)
{
	std::vector<std::string> vFiles, vDirs;
	SBakeDirScanUser User{&vFiles, &vDirs};
	pStorage->ListDirectory(IStorage::TYPE_ALL, pDir, BakeDirScan, &User);
	std::sort(vFiles.begin(), vFiles.end());
	std::sort(vDirs.begin(), vDirs.end());

	for(const std::string &File : vFiles)
		vOut.push_back(Prefix + File);
	for(const std::string &SubDir : vDirs)
	{
		char aSubPath[IO_MAX_PATH_LENGTH];
		str_format(aSubPath, sizeof(aSubPath), "%s/%s", pDir, SubDir.c_str());
		CollectBakeFiles(pStorage, aSubPath, Prefix + SubDir + "/", vOut);
	}
}

static bool CopyBakeFolder(IStorage *pStorage, const char *pSrc, const char *pDst, int &FilesCopied)
{
	std::vector<std::string> vFiles, vDirs;
	SBakeDirScanUser User{&vFiles, &vDirs};
	pStorage->ListDirectory(IStorage::TYPE_ALL, pSrc, BakeDirScan, &User);
	if(vFiles.empty() && vDirs.empty())
		return false;

	std::sort(vFiles.begin(), vFiles.end());
	std::sort(vDirs.begin(), vDirs.end());
	pStorage->CreateFolder(pDst, IStorage::TYPE_SAVE);

	for(const std::string &File : vFiles)
	{
		char aSrcPath[IO_MAX_PATH_LENGTH];
		char aDstPath[IO_MAX_PATH_LENGTH];
		str_format(aSrcPath, sizeof(aSrcPath), "%s/%s", pSrc, File.c_str());
		str_format(aDstPath, sizeof(aDstPath), "%s/%s", pDst, File.c_str());
		if(CopyBakeFile(pStorage, aSrcPath, aDstPath))
			FilesCopied++;
	}
	for(const std::string &SubDir : vDirs)
	{
		char aSrcPath[IO_MAX_PATH_LENGTH];
		char aDstPath[IO_MAX_PATH_LENGTH];
		str_format(aSrcPath, sizeof(aSrcPath), "%s/%s", pSrc, SubDir.c_str());
		str_format(aDstPath, sizeof(aDstPath), "%s/%s", pDst, SubDir.c_str());
		CopyBakeFolder(pStorage, aSrcPath, aDstPath, FilesCopied);
	}
	return true;
}

static bool BakeFolderSource(int Kind, const char *pPack, char *pDir, int DirSize, bool &SingleFile, IStorage *pStorage)
{
	SingleFile = false;
	if(Kind == BAKE_KIND_ENTITIES)
	{
		if(str_comp(pPack, "default") == 0)
		{
			str_copy(pDir, "editor/entities_clear", DirSize);
			return pStorage->FolderExists(pDir, IStorage::TYPE_ALL);
		}
		str_format(pDir, DirSize, "assets/entities/%s", pPack);
		if(pStorage->FolderExists(pDir, IStorage::TYPE_ALL))
			return true;
		str_format(pDir, DirSize, "assets/entities/%s.png", pPack);
		if(pStorage->FileExists(pDir, IStorage::TYPE_ALL))
		{
			SingleFile = true;
			return true;
		}
		return false;
	}

	if(str_comp(pPack, "default") == 0)
	{
		str_copy(pDir, "audio", DirSize);
		return pStorage->FolderExists(pDir, IStorage::TYPE_ALL);
	}
	str_format(pDir, DirSize, "assets/audio/%s", pPack);
	if(pStorage->FolderExists(pDir, IStorage::TYPE_ALL))
		return true;
	str_format(pDir, DirSize, "audio/%s", pPack);
	return pStorage->FolderExists(pDir, IStorage::TYPE_ALL);
}

static bool CopyBakeFile(IStorage *pStorage, const char *pSrc, const char *pDst)
{
	IOHANDLE In = pStorage->OpenFile(pSrc, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!In)
		return false;
	IOHANDLE Out = pStorage->OpenFile(pDst, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!Out)
	{
		io_close(In);
		return false;
	}

	char aBuffer[8192];
	bool Success = true;
	while(true)
	{
		const unsigned Read = io_read(In, aBuffer, sizeof(aBuffer));
		if(Read == 0)
			break;
		if(io_write(Out, aBuffer, Read) != Read)
		{
			Success = false;
			break;
		}
	}
	io_close(In);
	io_close(Out);
	return Success;
}

static bool LoadBakeImageFile(const char *pPath, CImageInfo &Image, IStorage *pStorage)
{
	IOHANDLE File = pStorage->OpenFile(pPath, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return false;
	int PngliteIncompatible = 0;
	return CImageLoader::LoadPng(File, pPath, Image, PngliteIncompatible);
}

static bool BakeEntitiesPack(const std::string &BasePack, const char *pFinalName, int &TilesOverridden, char *pStatus, int StatusSize, IStorage *pStorage)
{
	const bool BaseIsFolder = BakeEntitiesBaseIsFolder(BasePack.c_str(), pStorage);

	std::vector<const char *> vMods;
	if(BaseIsFolder)
	{
		for(const char *pMod : s_apBakeEntityMods)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "assets/entities/%s/%s.png", BasePack.c_str(), pMod);
			if(pStorage->FileExists(aPath, IStorage::TYPE_ALL))
				vMods.push_back(pMod);
		}
	}
	if(vMods.empty())
		vMods.push_back("ddnet");

	char aOutDir[IO_MAX_PATH_LENGTH];
	str_format(aOutDir, sizeof(aOutDir), "assets/entities/%s", pFinalName);
	pStorage->CreateFolder("assets", IStorage::TYPE_SAVE);
	pStorage->CreateFolder("assets/entities", IStorage::TYPE_SAVE);
	pStorage->CreateFolder(aOutDir, IStorage::TYPE_SAVE);

	std::map<std::string, std::unique_ptr<CImageInfo>> LoadedImages;
	bool AnySaved = false;

	for(const char *pMod : vMods)
	{
		char aBasePath[IO_MAX_PATH_LENGTH];
		if(BaseIsFolder)
			str_format(aBasePath, sizeof(aBasePath), "assets/entities/%s/%s.png", BasePack.c_str(), pMod);
		else
			str_format(aBasePath, sizeof(aBasePath), "assets/entities/%s.png", BasePack.c_str());

		CImageInfo BaseImage;
		if(!LoadBakeImageFile(aBasePath, BaseImage, pStorage))
			continue;

		CImageInfo Result;
		CopyToRgba(BaseImage, Result);
		BaseImage.Free();

		for(const SBakeOverride &Override : s_vBakeOverrides)
		{
			if(Override.m_Kind != BAKE_KIND_ENTITIES || Override.m_SourcePack == BasePack)
				continue;
			if(Override.m_SpriteId < 0 || Override.m_SpriteId >= BAKE_ENTITY_TILE_COUNT)
				continue;

			char aSourcePath[IO_MAX_PATH_LENGTH];
			if(!FindBakeEntitiesImage(Override.m_SourcePack.c_str(), pMod, aSourcePath, sizeof(aSourcePath), pStorage))
				continue;

			auto It = LoadedImages.find(aSourcePath);
			if(It == LoadedImages.end())
			{
				auto pImage = std::make_unique<CImageInfo>();
				if(!LoadBakeImageFile(aSourcePath, *pImage, pStorage))
					continue;
				It = LoadedImages.emplace(aSourcePath, std::move(pImage)).first;
			}

			BlitBakeTile(Result, *It->second, Override.m_SpriteId);
			TilesOverridden++;
		}

		char aOutputPath[IO_MAX_PATH_LENGTH];
		str_format(aOutputPath, sizeof(aOutputPath), "%s/%s.png", aOutDir, pMod);
		IOHANDLE File = pStorage->OpenFile(aOutputPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File || !CImageLoader::SavePng(File, aOutputPath, Result))
		{
			Result.Free();
			str_format(pStatus, StatusSize, "Could not write \"%s\".", aOutputPath);
			return false;
		}
		Result.Free();
		AnySaved = true;
	}

	if(!AnySaved)
	{
		str_format(pStatus, StatusSize, "Could not load the images of \"%s\".", BasePack.c_str());
		return false;
	}
	return true;
}

static void BakeSanitizeName(const char *pIn, char *pOut, int OutSize)
{
	char aRaw[64];
	int Out = 0;
	for(int i = 0; pIn[i] != '\0' && Out < (int)sizeof(aRaw) - 1; i++)
	{
		unsigned char c = (unsigned char)pIn[i];
		if(!std::isalnum(c) && c != '-' && c != '_' && c != ' ')
			c = '_';
		aRaw[Out++] = (char)c;
	}
	while(Out > 0 && aRaw[Out - 1] == ' ')
		Out--;
	aRaw[Out] = '\0';
	if(aRaw[0] == '\0')
		str_copy(aRaw, "custom", sizeof(aRaw));
	str_copy(pOut, aRaw, OutSize);
}

void CMenus::BakeGameSkin()
{
	BakeLoadPacks(s_BakeKind, Graphics(), Storage());
	const SBakeKind &KindInfo = s_aBakeKinds[s_BakeKind];
	const std::vector<SBakePackEntry> &vPacks = s_avBakePacks[s_BakeKind];

	if(vPacks.empty())
	{
		str_copy(s_aBakeStatus, "No asset packs found.", sizeof(s_aBakeStatus));
		return;
	}

	const std::string &BasePack = s_aBakeBasePack[s_BakeKind];
	if(BakeFindPackIndex(s_BakeKind, BasePack.c_str()) < 0)
	{
		str_copy(s_aBakeStatus, "Select a valid base pack first.", sizeof(s_aBakeStatus));
		return;
	}

	char aRequested[64];
	BakeSanitizeName(s_BakeOutputName.GetString(), aRequested, sizeof(aRequested));

	// Find a free output name.
	char aFinalName[64];
	str_copy(aFinalName, aRequested, sizeof(aFinalName));
	int Suffix = 1;
	auto NameExists = [&](const char *pName) {
		if(BakeFindPackIndex(s_BakeKind, pName) >= 0)
			return true;
		char aPath[IO_MAX_PATH_LENGTH];
		if(s_BakeKind == BAKE_KIND_ENTITIES)
			str_format(aPath, sizeof(aPath), "assets/entities/%s", pName);
		else if(s_BakeKind == BAKE_KIND_AUDIO)
			str_format(aPath, sizeof(aPath), "assets/audio/%s", pName);
		else
			str_format(aPath, sizeof(aPath), "%s/%s.png", KindInfo.m_pFolder, pName);
		return Storage()->FileExists(aPath, IStorage::TYPE_SAVE) || Storage()->FolderExists(aPath, IStorage::TYPE_SAVE);
	};
	while(NameExists(aFinalName))
	{
		str_format(aFinalName, sizeof(aFinalName), "%s_%d", aRequested, Suffix++);
		if(Suffix > 1000)
			break;
	}

	int FilesCopied = 0;
	int SpriteOverrides = 0;

	if(KindInfo.m_IsTiles)
	{
		if(!BakeEntitiesPack(BasePack, aFinalName, SpriteOverrides, s_aBakeStatus, sizeof(s_aBakeStatus), Storage()))
			return;
		FilesCopied = 1;
	}
	else if(KindInfo.m_IsSheet)
	{
		CImageInfo BaseImage;
		if(!LoadBakeSheetImage(s_BakeKind, BasePack.c_str(), BaseImage, Storage()))
		{
			str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not load base pack \"%s\".", BasePack.c_str());
			return;
		}

		CImageInfo Result;
		CopyToRgba(BaseImage, Result);
		BaseImage.Free();

		std::map<std::string, std::unique_ptr<CImageInfo>> LoadedImages;
		for(const SBakeOverride &Override : s_vBakeOverrides)
		{
			if(Override.m_Kind != s_BakeKind || Override.m_SourcePack == BasePack)
				continue;
			if(Override.m_SpriteId < 0 || Override.m_SpriteId >= g_pData->m_NumSprites)
				continue;

			auto It = LoadedImages.find(Override.m_SourcePack);
			if(It == LoadedImages.end())
			{
				auto pImage = std::make_unique<CImageInfo>();
				if(!LoadBakeSheetImage(s_BakeKind, Override.m_SourcePack.c_str(), *pImage, Storage()))
				{
					str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not load \"%s\" for %s.", Override.m_SourcePack.c_str(), g_pData->m_aSprites[Override.m_SpriteId].m_pName);
					Result.Free();
					return;
				}
				It = LoadedImages.emplace(Override.m_SourcePack, std::move(pImage)).first;
			}

			BlitBakeSprite(Result, *It->second, g_pData->m_aSprites[Override.m_SpriteId]);
			SpriteOverrides++;
		}

		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(KindInfo.m_pFolder, IStorage::TYPE_SAVE);
		char aOutputPath[IO_MAX_PATH_LENGTH];
		str_format(aOutputPath, sizeof(aOutputPath), "%s/%s.png", KindInfo.m_pFolder, aFinalName);
		IOHANDLE File = Storage()->OpenFile(aOutputPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
		if(!File || !CImageLoader::SavePng(File, aOutputPath, Result))
		{
			Result.Free();
			str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not write \"%s\".", aOutputPath);
			return;
		}
		Result.Free();
	}
	else
	{
		char aSource[IO_MAX_PATH_LENGTH];
		bool SingleFile = false;
		if(!BakeFolderSource(s_BakeKind, BasePack.c_str(), aSource, sizeof(aSource), SingleFile, Storage()))
		{
			str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not read base pack \"%s\".", BasePack.c_str());
			return;
		}

		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder(KindInfo.m_pFolder, IStorage::TYPE_SAVE);

		if(SingleFile)
		{
			char aOutputPath[IO_MAX_PATH_LENGTH];
			str_format(aOutputPath, sizeof(aOutputPath), "%s/%s.png", KindInfo.m_pFolder, aFinalName);
			if(!CopyBakeFile(Storage(), aSource, aOutputPath))
			{
				str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not write \"%s\".", aOutputPath);
				return;
			}
			FilesCopied = 1;
		}
		else
		{
			std::vector<std::string> vFiles;
			CollectBakeFiles(Storage(), aSource, "", vFiles);
			if(vFiles.empty())
			{
				str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Base pack \"%s\" contains no files.", BasePack.c_str());
				return;
			}

			char aOutputDir[IO_MAX_PATH_LENGTH];
			str_format(aOutputDir, sizeof(aOutputDir), "%s/%s", KindInfo.m_pFolder, aFinalName);
			CopyBakeFolder(Storage(), aSource, aOutputDir, FilesCopied);
			if(FilesCopied == 0)
			{
				str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not copy the files of \"%s\".", BasePack.c_str());
				return;
			}

			for(const SBakeFileOverride &Override : s_vBakeFileOverrides)
			{
				if(Override.m_Kind != s_BakeKind)
					continue;

				char aSourceDir[IO_MAX_PATH_LENGTH];
				bool UnusedSingleFile = false;
				if(!BakeFolderSource(s_BakeKind, Override.m_SourcePack.c_str(), aSourceDir, sizeof(aSourceDir), UnusedSingleFile, Storage()))
					continue;

				char aSourcePath[IO_MAX_PATH_LENGTH];
				char aOutputPath[IO_MAX_PATH_LENGTH];
				str_format(aSourcePath, sizeof(aSourcePath), "%s/%s", aSourceDir, Override.m_File.c_str());
				str_format(aOutputPath, sizeof(aOutputPath), "%s/%s", aOutputDir, Override.m_File.c_str());
				if(!Storage()->FileExists(aSourcePath, IStorage::TYPE_ALL))
					continue;
				CreateBakeParentFolders(Storage(), aOutputPath);
				CopyBakeFile(Storage(), aSourcePath, aOutputPath);
			}
		}
	}

	switch(s_BakeKind)
	{
	case BAKE_KIND_GAME:
		str_copy(g_Config.m_ClAssetGame, aFinalName);
		GameClient()->LoadGameSkin(aFinalName);
		ClearCustomItems(ASSETS_TAB_GAME);
		break;
	case BAKE_KIND_EMOTICONS:
		str_copy(g_Config.m_ClAssetEmoticons, aFinalName);
		GameClient()->LoadEmoticonsSkin(aFinalName);
		ClearCustomItems(ASSETS_TAB_EMOTICONS);
		break;
	case BAKE_KIND_PARTICLES:
		str_copy(g_Config.m_ClAssetParticles, aFinalName);
		GameClient()->LoadParticlesSkin(aFinalName);
		ClearCustomItems(ASSETS_TAB_PARTICLES);
		break;
	case BAKE_KIND_HUD:
		str_copy(g_Config.m_ClAssetHud, aFinalName);
		GameClient()->LoadHudSkin(aFinalName);
		ClearCustomItems(ASSETS_TAB_HUD);
		break;
	case BAKE_KIND_EXTRAS:
		str_copy(g_Config.m_ClAssetExtras, aFinalName);
		GameClient()->LoadExtrasSkin(aFinalName);
		ClearCustomItems(ASSETS_TAB_EXTRAS);
		break;
	case BAKE_KIND_ENTITIES:
		str_copy(g_Config.m_ClAssetsEntities, aFinalName);
		GameClient()->m_MapImages.ChangeEntitiesPath(aFinalName);
		ClearCustomItems(ASSETS_TAB_ENTITIES);
		break;
	case BAKE_KIND_AUDIO:
		str_copy(g_Config.m_SndPack, aFinalName);
		GameClient()->m_Sounds.Clear();
		ClearCustomItems(ASSETS_TAB_AUDIO);
		break;
	default:
		dbg_assert_failed("Invalid bake kind %d", s_BakeKind);
		break;
	}

	BakeUnloadPacks(Graphics());
	BakeLoadPacks(s_BakeKind, Graphics(), Storage());
	s_BakeOutputName.Set(aFinalName);

	if(KindInfo.m_IsTiles)
		str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Baked \"%s\" with %d tile overrides.", aFinalName, SpriteOverrides);
	else if(KindInfo.m_IsSheet)
		str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Baked \"%s\" with %d sprite overrides.", aFinalName, SpriteOverrides);
	else
		str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Baked \"%s\" with %d files.", aFinalName, FilesCopied);
}

static void BakeSpriteDisplayName(const char *pName, char *pBuffer, int BufferSize)
{
	static const char *const s_apPrefixes[] = {"weapon_", "pickup_", "hud_", "part_"};
	const char *pDisplay = pName;
	for(const char *pPrefix : s_apPrefixes)
	{
		if(str_startswith(pName, pPrefix) != nullptr)
		{
			pDisplay = pName + str_length(pPrefix);
			break;
		}
	}

	int Out = 0;
	bool Capitalize = true;
	for(int i = 0; pDisplay[i] != '\0' && Out < BufferSize - 1; i++)
	{
		char c = pDisplay[i];
		if(c == '_')
		{
			c = ' ';
			Capitalize = true;
		}
		else if(Capitalize)
		{
			c = (char)std::toupper((unsigned char)c);
			Capitalize = false;
		}
		else if(c == ' ')
		{
			Capitalize = true;
		}
		pBuffer[Out++] = c;
	}
	pBuffer[Out] = '\0';
	if(pBuffer[0] == '\0')
		str_copy(pBuffer, pName, BufferSize);
}

void CMenus::RenderAssetBaker(CUIRect MainView)
{
	if(!s_BakeStateInitialized)
	{
		s_BakeStateInitialized = true;
		s_BakeOutputName.Set("custom");
		s_aBakeBasePack[BAKE_KIND_GAME] = g_Config.m_ClAssetGame;
		s_aBakeBasePack[BAKE_KIND_EMOTICONS] = g_Config.m_ClAssetEmoticons;
		s_aBakeBasePack[BAKE_KIND_PARTICLES] = g_Config.m_ClAssetParticles;
		s_aBakeBasePack[BAKE_KIND_HUD] = g_Config.m_ClAssetHud;
		s_aBakeBasePack[BAKE_KIND_EXTRAS] = g_Config.m_ClAssetExtras;
		s_aBakeBasePack[BAKE_KIND_ENTITIES] = g_Config.m_ClAssetsEntities;
		s_aBakeBasePack[BAKE_KIND_AUDIO] = g_Config.m_SndPack;
	}
	if(s_BakeKind < 0 || s_BakeKind >= BAKE_KIND_COUNT)
		s_BakeKind = BAKE_KIND_GAME;

	BakeLoadPacks(s_BakeKind, Graphics(), Storage());
	const SBakeKind &KindInfo = s_aBakeKinds[s_BakeKind];
	std::vector<SBakePackEntry> &vPacks = s_avBakePacks[s_BakeKind];

	CUIRect Header, Body, Status;
	MainView.HSplitTop(24.0f, &Header, &Body);
	Body.HSplitBottom(18.0f, &Body, &Status);

	CUIRect KindLabel, KindDropDown, BaseLabel, BaseDropDown, NameLabel, NameEdit, BakeButton, ReloadButton;
	Header.VSplitRight(70.0f, &Header, &ReloadButton);
	Header.VSplitRight(6.0f, &Header, nullptr);
	Header.VSplitRight(84.0f, &Header, &BakeButton);
	Header.VSplitRight(6.0f, &Header, nullptr);
	Header.VSplitRight(120.0f, &Header, &NameEdit);
	Header.VSplitRight(74.0f, &Header, &NameLabel);
	Header.VSplitLeft(42.0f, &KindLabel, &Header);
	Header.VSplitLeft(std::min(140.0f, Header.w * 0.28f), &KindDropDown, &Header);
	Header.VSplitLeft(8.0f, nullptr, &Header);
	Header.VSplitLeft(64.0f, &BaseLabel, &Header);
	Header.VSplitLeft(std::min(140.0f, Header.w * 0.4f), &BaseDropDown, &Header);

	Ui()->DoLabel(&KindLabel, Localize("Asset"), 11.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&BaseLabel, Localize("Base pack"), 11.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&NameLabel, Localize("Output name"), 11.0f, TEXTALIGN_ML);

	const char *apKindNames[BAKE_KIND_COUNT];
	for(int i = 0; i < BAKE_KIND_COUNT; i++)
		apKindNames[i] = Localize(s_aBakeKinds[i].m_pLabel);
	s_BakeKindDropDown.m_SelectionPopupContext.m_pScrollRegion = &s_BakeKindScrollRegion;
	const int NewKind = Ui()->DoDropDown(&KindDropDown, s_BakeKind, apKindNames, BAKE_KIND_COUNT, s_BakeKindDropDown);
	if(NewKind != s_BakeKind && NewKind >= 0 && NewKind < BAKE_KIND_COUNT)
	{
		s_BakeKind = NewKind;
		s_aBakeStatus[0] = '\0';
		// The rest of this frame uses pack data for the previous kind.
		return;
	}

	std::vector<const char *> vpPackNames;
	vpPackNames.reserve(vPacks.size());
	int BasePackIndex = 0;
	for(size_t i = 0; i < vPacks.size(); i++)
	{
		vpPackNames.push_back(vPacks[i].m_Name.c_str());
		if(str_comp(vPacks[i].m_Name.c_str(), s_aBakeBasePack[s_BakeKind].c_str()) == 0)
			BasePackIndex = (int)i;
	}
	if(!vpPackNames.empty())
	{
		s_aBakeBaseDropDown[s_BakeKind].m_SelectionPopupContext.m_pScrollRegion = &s_aBakeBaseScrollRegion[s_BakeKind];
		const int NewBasePack = Ui()->DoDropDown(&BaseDropDown, BasePackIndex, vpPackNames.data(), (int)vpPackNames.size(), s_aBakeBaseDropDown[s_BakeKind]);
		if(NewBasePack >= 0 && NewBasePack < (int)vPacks.size())
			s_aBakeBasePack[s_BakeKind] = vPacks[NewBasePack].m_Name;
	}

	Ui()->DoEditBox(&s_BakeOutputName, &NameEdit, 11.0f);

	static CButtonContainer s_BakeButtonId;
	if(DoButton_Menu(&s_BakeButtonId, Localize("Bake"), 0, &BakeButton))
		BakeGameSkin();

	static CButtonContainer s_BakeReloadId;
	if(DoButton_Menu(&s_BakeReloadId, Localize("Reload"), 0, &ReloadButton))
	{
		BakeUnloadPacks(Graphics());
		s_aBakeStatus[0] = '\0';
		// The pack list is now empty; it is reloaded at the start of the next
		// frame, so do not touch it for the remainder of this one.
		return;
	}

	Ui()->DoLabel(&Status, s_aBakeStatus, 9.5f, TEXTALIGN_ML);

	if(vPacks.empty())
	{
		Ui()->DoLabel(&Body, Localize("No asset packs found."), 10.0f, TEXTALIGN_ML);
		return;
	}

	if(KindInfo.m_IsSheet || KindInfo.m_IsTiles)
	{
		const std::vector<SBakeCategory> &vCategories = GetBakeCategories(s_BakeKind);
		if(vCategories.empty())
		{
			Ui()->DoLabel(&Body, KindInfo.m_IsTiles ? Localize("No tiles found for this asset.") : Localize("No sprites found for this asset."), 10.0f, TEXTALIGN_ML);
			return;
		}
		if(s_aBakeCategory[s_BakeKind] < 0 || s_aBakeCategory[s_BakeKind] >= (int)vCategories.size())
			s_aBakeCategory[s_BakeKind] = 0;

		CUIRect CategoryList, Items;
		Body.VSplitLeft(160.0f, &CategoryList, &Items);
		CategoryList.VSplitRight(8.0f, &CategoryList, nullptr);

		CScrollRegionParams CategoryParams;
		CategoryParams.m_ScrollUnit = 24.0f;
		CategoryParams.m_ScrollbarThickness = 8.0f;
		CategoryParams.m_ScrollbarMargin = 2.0f;
		s_BakeCategoryScroll.Begin(&CategoryList, &CategoryParams);
		for(int i = 0; i < (int)vCategories.size(); i++)
		{
			CUIRect Button;
			CategoryList.HSplitTop(22.0f, &Button, &CategoryList);
			CategoryList.HSplitTop(3.0f, nullptr, &CategoryList);
			if(!s_BakeCategoryScroll.AddRect(Button))
				continue;
			if(DoButton_MenuTab(&s_aBakeCategoryButtons[i], Localize(vCategories[i].m_pName), i == s_aBakeCategory[s_BakeKind], &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
				s_aBakeCategory[s_BakeKind] = i;
		}
		s_BakeCategoryScroll.End();

		const SBakeCategory &Category = vCategories[s_aBakeCategory[s_BakeKind]];
		if(Category.m_vSprites.empty())
		{
			Ui()->DoLabel(&Items, KindInfo.m_IsTiles ? Localize("No tiles found for this category.") : Localize("No sprites found for this category."), 10.0f, TEXTALIGN_ML);
			return;
		}

		CUIRect ItemsHeader;
		Items.HSplitTop(18.0f, &ItemsHeader, &Items);
		Ui()->DoLabel(&ItemsHeader, KindInfo.m_IsTiles ? Localize("Pick a source pack per tile. \"Base\" keeps the base pack's tile.") : Localize("Pick a source pack per sprite. \"Base\" keeps the base pack's sprite."), 10.0f, TEXTALIGN_ML);

		std::vector<const char *> vpSourceNames;
		vpSourceNames.reserve(vPacks.size() + 1);
		vpSourceNames.push_back(Localize("Base"));
		for(const SBakePackEntry &Pack : vPacks)
			vpSourceNames.push_back(Pack.m_Name.c_str());

		const std::string &BasePackName = s_aBakeBasePack[s_BakeKind];
		int BaseTextureIndex = BakeFindPackIndex(s_BakeKind, BasePackName.c_str());
		if(BaseTextureIndex < 0)
			BaseTextureIndex = 0;

		CScrollRegionParams ItemParams;
		ItemParams.m_ScrollUnit = 3.0f * 26.0f;
		ItemParams.m_ScrollbarThickness = 10.0f;
		ItemParams.m_ScrollbarMargin = 3.0f;
		ItemParams.m_ForceShowScrollbar = true;
		s_BakeItemScroll.Begin(&Items, &ItemParams);

		for(int SpriteId : Category.m_vSprites)
		{
			if(KindInfo.m_IsTiles)
			{
				if(SpriteId < 0 || SpriteId >= BAKE_ENTITY_TILE_COUNT)
					continue;
			}
			else if(SpriteId < 0 || SpriteId >= g_pData->m_NumSprites)
				continue;

			CUIRect Row;
			Items.HSplitTop(24.0f, &Row, &Items);
			Items.HSplitTop(2.0f, nullptr, &Items);
			if(!s_BakeItemScroll.AddRect(Row))
				continue;

			int OverrideIndex = BakeFindOverride(s_BakeKind, SpriteId);
			int CurrentSource = 0;
			if(OverrideIndex >= 0)
			{
				const int PackIndex = BakeFindPackIndex(s_BakeKind, s_vBakeOverrides[OverrideIndex].m_SourcePack.c_str());
				if(PackIndex >= 0)
					CurrentSource = PackIndex + 1;
			}

			CUIRect Thumb, LabelRect, DropDown;
			Row.VSplitLeft(24.0f, &Thumb, &Row);
			Row.VSplitRight(190.0f, &LabelRect, &DropDown);
			DropDown.VSplitLeft(6.0f, nullptr, &DropDown);

			IGraphics::CTextureHandle Texture = vPacks[BaseTextureIndex].m_Texture;
			if(CurrentSource > 0)
			{
				const int PackIndex = CurrentSource - 1;
				if(PackIndex >= 0 && PackIndex < (int)vPacks.size() && vPacks[PackIndex].m_Texture.IsValid() && !vPacks[PackIndex].m_Texture.IsNullTexture())
					Texture = vPacks[PackIndex].m_Texture;
			}
			if(KindInfo.m_IsTiles)
			{
				DrawBakeTilePreview(Graphics(), Texture, SpriteId, Thumb);
			}
			else
			{
				const std::vector<int> vOne{SpriteId};
				DrawBakePreview(Graphics(), Texture, vOne, Thumb);
			}

			char aDisplayName[128];
			if(KindInfo.m_IsTiles)
			{
				const char *pTileName = BakeEntityTileName(SpriteId);
				if(pTileName != nullptr)
					str_copy(aDisplayName, pTileName, sizeof(aDisplayName));
				else
					str_format(aDisplayName, sizeof(aDisplayName), "Entity %d", SpriteId - ENTITY_OFFSET);
			}
			else
			{
				const CDataSprite &Sprite = g_pData->m_aSprites[SpriteId];
				BakeSpriteDisplayName(Sprite.m_pName, aDisplayName, sizeof(aDisplayName));
			}
			SLabelProperties Props;
			Props.m_MaxWidth = LabelRect.w;
			Props.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&LabelRect, aDisplayName, 10.0f, TEXTALIGN_ML, Props);

			if(SpriteId < (int)(sizeof(s_aBakeSpriteDropDown) / sizeof(s_aBakeSpriteDropDown[0])))
			{
				s_aBakeSpriteDropDown[SpriteId].m_SelectionPopupContext.m_pScrollRegion = &s_aBakeSpriteScrollRegion[SpriteId];
				const int NewSource = Ui()->DoDropDown(&DropDown, CurrentSource, vpSourceNames.data(), (int)vpSourceNames.size(), s_aBakeSpriteDropDown[SpriteId]);
				if(NewSource != CurrentSource && NewSource >= 0)
				{
					if(NewSource == 0)
					{
						if(OverrideIndex >= 0)
							s_vBakeOverrides.erase(s_vBakeOverrides.begin() + OverrideIndex);
					}
					else
					{
						const int PackIndex = NewSource - 1;
						if(PackIndex >= 0 && PackIndex < (int)vPacks.size())
						{
							if(OverrideIndex >= 0)
								s_vBakeOverrides[OverrideIndex].m_SourcePack = vPacks[PackIndex].m_Name;
							else
								s_vBakeOverrides.push_back({s_BakeKind, SpriteId, vPacks[PackIndex].m_Name});
						}
					}
				}
			}
		}
		s_BakeItemScroll.End();
	}
	else
	{
		CUIRect Info;
		Body.HSplitTop(20.0f, &Info, &Body);
		char aInfo[256];
		str_format(aInfo, sizeof(aInfo), Localize("Pick a source pack per file. \"Base\" keeps the base pack's file."), Localize(KindInfo.m_pLabel));
		Ui()->DoLabel(&Info, aInfo, 10.0f, TEXTALIGN_ML);

		char aSource[IO_MAX_PATH_LENGTH];
		bool SingleFile = false;
		if(BakeFolderSource(s_BakeKind, s_aBakeBasePack[s_BakeKind].c_str(), aSource, sizeof(aSource), SingleFile, Storage()))
		{
			std::vector<std::string> vFiles;
			if(SingleFile)
			{
				const char *pFileName = str_rchr(aSource, '/');
				vFiles.push_back(pFileName != nullptr ? pFileName + 1 : aSource);
			}
			else
			{
				CollectBakeFiles(Storage(), aSource, "", vFiles);
			}

			if(vFiles.empty())
			{
				Ui()->DoLabel(&Body, Localize("This base pack contains no files."), 10.0f, TEXTALIGN_ML);
				return;
			}

			std::vector<const char *> vpSourceNames;
			vpSourceNames.reserve(vPacks.size() + 1);
			vpSourceNames.push_back(Localize("Base"));
			for(const SBakePackEntry &Pack : vPacks)
				vpSourceNames.push_back(Pack.m_Name.c_str());

			std::map<std::string, CUi::SDropDownState> &FileDropDowns = s_aBakeFileDropDown[s_BakeKind];

			CScrollRegionParams FolderParams;
			FolderParams.m_ScrollUnit = 3.0f * 20.0f;
			FolderParams.m_ScrollbarThickness = 10.0f;
			FolderParams.m_ScrollbarMargin = 3.0f;
			s_BakeFolderScroll.Begin(&Body, &FolderParams);
			for(const std::string &File : vFiles)
			{
				CUIRect Row;
				Body.HSplitTop(20.0f, &Row, &Body);
				if(!s_BakeFolderScroll.AddRect(Row))
					continue;

				int OverrideIndex = BakeFindFileOverride(s_BakeKind, File.c_str());
				int CurrentSource = 0;
				if(OverrideIndex >= 0)
				{
					const int PackIndex = BakeFindPackIndex(s_BakeKind, s_vBakeFileOverrides[OverrideIndex].m_SourcePack.c_str());
					if(PackIndex >= 0)
						CurrentSource = PackIndex + 1;
				}

				CUIRect LabelRect, DropDown;
				Row.VSplitRight(190.0f, &LabelRect, &DropDown);
				DropDown.VSplitLeft(6.0f, nullptr, &DropDown);

				SLabelProperties Props;
				Props.m_MaxWidth = LabelRect.w;
				Props.m_EllipsisAtEnd = true;
				Ui()->DoLabel(&LabelRect, File.c_str(), 10.0f, TEXTALIGN_ML, Props);

				CUi::SDropDownState &DropDownState = FileDropDowns[File];
				DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_BakeFilePopupScroll[s_BakeKind];
				const int NewSource = Ui()->DoDropDown(&DropDown, CurrentSource, vpSourceNames.data(), (int)vpSourceNames.size(), DropDownState);
				if(NewSource != CurrentSource && NewSource >= 0)
				{
					if(NewSource == 0)
					{
						if(OverrideIndex >= 0)
							s_vBakeFileOverrides.erase(s_vBakeFileOverrides.begin() + OverrideIndex);
					}
					else
					{
						const int PackIndex = NewSource - 1;
						if(PackIndex >= 0 && PackIndex < (int)vPacks.size())
						{
							if(OverrideIndex >= 0)
								s_vBakeFileOverrides[OverrideIndex].m_SourcePack = vPacks[PackIndex].m_Name;
							else
								s_vBakeFileOverrides.push_back({s_BakeKind, File, vPacks[PackIndex].m_Name});
						}
					}
				}
			}
			s_BakeFolderScroll.End();
		}
		else
		{
			Ui()->DoLabel(&Body, Localize("Select a valid base pack to see its files."), 10.0f, TEXTALIGN_ML);
		}
	}
}

void CMenus::ConchainAssetsEntities(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData){
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetsEntities) != 0)
		{
			pThis->GameClient()->m_MapImages.ChangeEntitiesPath(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetGame(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetGame) != 0)
		{
			pThis->GameClient()->LoadGameSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetParticles(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetParticles) != 0)
		{
			pThis->GameClient()->LoadParticlesSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetEmoticons(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetEmoticons) != 0)
		{
			pThis->GameClient()->LoadEmoticonsSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetHud(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetHud) != 0)
		{
			pThis->GameClient()->LoadHudSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainAssetExtras(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	if(pResult->NumArguments() == 1)
	{
		const char *pArg = pResult->GetString(0);
		if(str_comp(pArg, g_Config.m_ClAssetExtras) != 0)
		{
			pThis->GameClient()->LoadExtrasSkin(pArg);
		}
	}

	pfnCallback(pResult, pCallbackUserData);
}

void CMenus::ConchainSndPack(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CMenus *pThis = (CMenus *)pUserData;
	char aOldSndPack[64];
	str_copy(aOldSndPack, g_Config.m_SndPack, sizeof(aOldSndPack));
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() == 1 && str_comp(aOldSndPack, g_Config.m_SndPack) != 0)
	{
		pThis->GameClient()->m_Sounds.Clear();
	}
}
