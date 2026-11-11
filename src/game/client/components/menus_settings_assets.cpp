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

struct SAssetBakeSlot
{
	const char *m_pName;
	std::vector<int> m_vSprites;
};

static const std::vector<SAssetBakeSlot> &GetAssetBakeSlots()
{
	static const std::vector<SAssetBakeSlot> s_vSlots = {
		{"Crosshairs", {SPRITE_WEAPON_HAMMER_CURSOR, SPRITE_WEAPON_GUN_CURSOR, SPRITE_WEAPON_SHOTGUN_CURSOR, SPRITE_WEAPON_GRENADE_CURSOR, SPRITE_WEAPON_LASER_CURSOR, SPRITE_WEAPON_NINJA_CURSOR}},
		{"Health & armor", {SPRITE_HEALTH_FULL, SPRITE_HEALTH_EMPTY, SPRITE_ARMOR_FULL, SPRITE_ARMOR_EMPTY}},
		{"Weapon bodies", {SPRITE_WEAPON_HAMMER_BODY, SPRITE_WEAPON_GUN_BODY, SPRITE_WEAPON_SHOTGUN_BODY, SPRITE_WEAPON_GRENADE_BODY, SPRITE_WEAPON_LASER_BODY, SPRITE_WEAPON_NINJA_BODY}},
		{"Projectiles", {SPRITE_WEAPON_GUN_PROJ, SPRITE_WEAPON_SHOTGUN_PROJ, SPRITE_WEAPON_GRENADE_PROJ, SPRITE_WEAPON_LASER_PROJ}},
		{"Muzzles", {SPRITE_WEAPON_GUN_MUZZLE1, SPRITE_WEAPON_GUN_MUZZLE2, SPRITE_WEAPON_GUN_MUZZLE3, SPRITE_WEAPON_SHOTGUN_MUZZLE1, SPRITE_WEAPON_SHOTGUN_MUZZLE2, SPRITE_WEAPON_SHOTGUN_MUZZLE3, SPRITE_WEAPON_NINJA_MUZZLE1, SPRITE_WEAPON_NINJA_MUZZLE2, SPRITE_WEAPON_NINJA_MUZZLE3}},
		{"Hook", {SPRITE_HOOK_CHAIN, SPRITE_HOOK_HEAD}},
		{"Pickups", {SPRITE_PICKUP_HEALTH, SPRITE_PICKUP_FREEZE, SPRITE_PICKUP_ARMOR, SPRITE_PICKUP_HAMMER, SPRITE_PICKUP_GUN, SPRITE_PICKUP_SHOTGUN, SPRITE_PICKUP_GRENADE, SPRITE_PICKUP_LASER, SPRITE_PICKUP_NINJA, SPRITE_PICKUP_ARMOR_SHOTGUN, SPRITE_PICKUP_ARMOR_GRENADE, SPRITE_PICKUP_ARMOR_NINJA, SPRITE_PICKUP_ARMOR_LASER}},
		{"Flags", {SPRITE_FLAG_BLUE, SPRITE_FLAG_RED}},
		{"Particles & stars", {SPRITE_PART1, SPRITE_PART2, SPRITE_PART3, SPRITE_PART4, SPRITE_PART5, SPRITE_PART6, SPRITE_PART7, SPRITE_PART8, SPRITE_PART9, SPRITE_STAR1, SPRITE_STAR2, SPRITE_STAR3}},
	};
	return s_vSlots;
}

static std::vector<std::string> s_vBakeSlotPacks;
static std::string s_BakeBasePack;
static int s_BakeCurrentSlot = 0;
static bool s_BakeStateInitialized = false;
static char s_aBakeStatus[256] = "";
static CLineInputBuffered<64> s_BakeOutputName;
static CUi::SDropDownState s_BakeBaseDropDown;
static CButtonContainer s_aBakeSlotButtons[32];

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

static bool FindGameAssetPath(const char *pName, char *pBuffer, int BufferSize, IStorage *pStorage)
{
	if(str_comp(pName, "default") == 0)
	{
		str_copy(pBuffer, "game.png", BufferSize);
		return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
	}

	str_format(pBuffer, BufferSize, "assets/game/%s.png", pName);
	if(pStorage->FileExists(pBuffer, IStorage::TYPE_ALL))
		return true;

	str_format(pBuffer, BufferSize, "assets/game/%s/game.png", pName);
	return pStorage->FileExists(pBuffer, IStorage::TYPE_ALL);
}

static bool LoadGameAssetImage(const char *pName, CImageInfo &Image, IStorage *pStorage)
{
	char aPath[IO_MAX_PATH_LENGTH];
	if(!FindGameAssetPath(pName, aPath, sizeof(aPath), pStorage))
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

static void DrawBakePreview(IGraphics *pGraphics, IGraphics::CTextureHandle Texture, const std::vector<int> &vSprites, CUIRect Area)
{
	if(!Texture.IsValid())
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

void CMenus::BakeGameSkin()
{
	const std::vector<SAssetBakeSlot> &vSlots = GetAssetBakeSlots();
	if(s_vBakeSlotPacks.size() != vSlots.size())
		s_vBakeSlotPacks.resize(vSlots.size());

	const SCustomGame *pBasePack = nullptr;
	for(const SCustomGame &Pack : m_vGameList)
	{
		if(str_comp(Pack.m_aName, s_BakeBasePack.c_str()) == 0)
		{
			pBasePack = &Pack;
			break;
		}
	}
	if(pBasePack == nullptr)
	{
		str_copy(s_aBakeStatus, "Select a valid base pack first.", sizeof(s_aBakeStatus));
		return;
	}

	CImageInfo BaseImage;
	if(!LoadGameAssetImage(s_BakeBasePack.c_str(), BaseImage, Storage()))
	{
		str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not load base pack \"%s\".", s_BakeBasePack.c_str());
		return;
	}

	CImageInfo Result;
	CopyToRgba(BaseImage, Result);
	BaseImage.Free();

	std::map<std::string, std::unique_ptr<CImageInfo>> LoadedImages;
	int OverrideCount = 0;
	for(size_t i = 0; i < vSlots.size(); ++i)
	{
		const std::string &SelectedPack = s_vBakeSlotPacks[i];
		if(SelectedPack.empty() || SelectedPack == s_BakeBasePack)
			continue;

		auto It = LoadedImages.find(SelectedPack);
		if(It == LoadedImages.end())
		{
			auto pImage = std::make_unique<CImageInfo>();
			if(!LoadGameAssetImage(SelectedPack.c_str(), *pImage, Storage()))
			{
				str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not load \"%s\" for %s.", SelectedPack.c_str(), vSlots[i].m_pName);
				Result.Free();
				return;
			}
			It = LoadedImages.emplace(SelectedPack, std::move(pImage)).first;
		}

		for(int SpriteId : vSlots[i].m_vSprites)
			BlitBakeSprite(Result, *It->second, g_pData->m_aSprites[SpriteId]);
		OverrideCount++;
	}

	char aName[64];
	str_copy(aName, s_BakeOutputName.GetString(), sizeof(aName));
	if(aName[0] == '\0')
		str_copy(aName, "custom", sizeof(aName));
	for(char *p = aName; *p != '\0'; ++p)
	{
		const unsigned char c = (unsigned char)*p;
		if(!std::isalnum(c) && c != '-' && c != '_' && c != ' ')
			*p = '_';
	}

	char aFinalName[64];
	str_copy(aFinalName, aName, sizeof(aFinalName));
	int Suffix = 1;
	auto NameExists = [&](const char *pName) {
		for(const SCustomGame &Pack : m_vGameList)
		{
			if(str_comp(Pack.m_aName, pName) == 0)
				return true;
		}
		char aPath[IO_MAX_PATH_LENGTH];
		str_format(aPath, sizeof(aPath), "assets/game/%s.png", pName);
		return Storage()->FileExists(aPath, IStorage::TYPE_SAVE);
	};
	while(NameExists(aFinalName))
	{
		str_format(aFinalName, sizeof(aFinalName), "%s_%d", aName, Suffix++);
		if(Suffix > 1000)
			break;
	}

	Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("assets/game", IStorage::TYPE_SAVE);
	char aOutputPath[IO_MAX_PATH_LENGTH];
	str_format(aOutputPath, sizeof(aOutputPath), "assets/game/%s.png", aFinalName);
	IOHANDLE File = Storage()->OpenFile(aOutputPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File || !CImageLoader::SavePng(File, aOutputPath, Result))
	{
		Result.Free();
		str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Could not write \"%s\".", aOutputPath);
		return;
	}
	Result.Free();

	str_copy(g_Config.m_ClAssetGame, aFinalName);
	s_BakeOutputName.Set(aFinalName);
	ClearCustomItems(ASSETS_TAB_GAME);

	// Rebuild the pack list immediately so the rest of this frame can use it.
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = []() {};
	SCustomGame DefaultPack;
	str_copy(DefaultPack.m_aName, "default");
	LoadAsset(&DefaultPack, "game", Graphics());
	m_vGameList.push_back(DefaultPack);
	Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/game", GameScan, &User);
	std::sort(m_vGameList.begin(), m_vGameList.end());

	str_format(s_aBakeStatus, sizeof(s_aBakeStatus), "Baked \"%s\" with %d overrides.", aFinalName, OverrideCount);
}

void CMenus::RenderAssetBaker(CUIRect MainView)
{
	SMenuAssetScanUser User;
	User.m_pUser = this;
	User.m_LoadedFunc = []() {};

	if(m_vGameList.empty())
	{
		SCustomGame DefaultPack;
		str_copy(DefaultPack.m_aName, "default");
		LoadAsset(&DefaultPack, "game", Graphics());
		m_vGameList.push_back(DefaultPack);

		Storage()->ListDirectory(IStorage::TYPE_ALL, "assets/game", GameScan, &User);
		std::sort(m_vGameList.begin(), m_vGameList.end());
	}

	const std::vector<SAssetBakeSlot> &vSlots = GetAssetBakeSlots();
	if(s_vBakeSlotPacks.size() != vSlots.size())
		s_vBakeSlotPacks.resize(vSlots.size());
	if(!s_BakeStateInitialized)
	{
		s_BakeStateInitialized = true;
		s_BakeBasePack = g_Config.m_ClAssetGame;
		s_BakeOutputName.Set("custom");
	}
	if(s_BakeCurrentSlot < 0 || s_BakeCurrentSlot >= (int)vSlots.size())
		s_BakeCurrentSlot = 0;

	bool BasePackExists = false;
	for(const SCustomGame &Pack : m_vGameList)
	{
		if(str_comp(Pack.m_aName, s_BakeBasePack.c_str()) == 0)
		{
			BasePackExists = true;
			break;
		}
	}
	if(!BasePackExists)
		s_BakeBasePack = m_vGameList.front().m_aName;

	for(std::string &SelectedPack : s_vBakeSlotPacks)
	{
		if(SelectedPack.empty())
			continue;
		bool Exists = false;
		for(const SCustomGame &Pack : m_vGameList)
		{
			if(SelectedPack == Pack.m_aName)
			{
				Exists = true;
				break;
			}
		}
		if(!Exists)
			SelectedPack.clear();
	}

	CUIRect Header, Body, Status;
	MainView.HSplitTop(24.0f, &Header, &Body);
	Body.HSplitBottom(18.0f, &Body, &Status);

	CUIRect BaseLabel, BaseDropDown, NameLabel, NameEdit, BakeButton, ReloadButton;
	Header.VSplitLeft(64.0f, &BaseLabel, &Header);
	Header.VSplitLeft(std::min(210.0f, Header.w * 0.3f), &BaseDropDown, &Header);
	Header.VSplitLeft(8.0f, nullptr, &Header);
	Header.VSplitLeft(72.0f, &NameLabel, &Header);
	Header.VSplitLeft(std::min(170.0f, Header.w * 0.3f), &NameEdit, &Header);
	Header.VSplitRight(70.0f, &Header, &ReloadButton);
	Header.VSplitRight(6.0f, nullptr, &Header);
	Header.VSplitRight(90.0f, &Header, &BakeButton);

	Ui()->DoLabel(&BaseLabel, "Base pack", 11.0f, TEXTALIGN_ML);
	Ui()->DoLabel(&NameLabel, "Output name", 11.0f, TEXTALIGN_ML);

	std::vector<const char *> vpPackNames;
	vpPackNames.reserve(m_vGameList.size());
	int BasePackIndex = 0;
	for(size_t i = 0; i < m_vGameList.size(); ++i)
	{
		vpPackNames.push_back(m_vGameList[i].m_aName);
		if(str_comp(m_vGameList[i].m_aName, s_BakeBasePack.c_str()) == 0)
			BasePackIndex = (int)i;
	}
	const int NewBasePack = Ui()->DoDropDown(&BaseDropDown, BasePackIndex, vpPackNames.data(), (int)vpPackNames.size(), s_BakeBaseDropDown);
	if(NewBasePack >= 0 && NewBasePack < (int)m_vGameList.size())
		s_BakeBasePack = m_vGameList[NewBasePack].m_aName;

	Ui()->DoEditBox(&s_BakeOutputName, &NameEdit, 11.0f);

	static CButtonContainer s_BakeButtonId;
	if(DoButton_Menu(&s_BakeButtonId, Localize("Bake"), 0, &BakeButton))
		BakeGameSkin();

	static CButtonContainer s_BakeReloadId;
	if(DoButton_Menu(&s_BakeReloadId, Localize("Reload"), 0, &ReloadButton))
		ClearCustomItems(ASSETS_TAB_GAME);

	Ui()->DoLabel(&Status, s_aBakeStatus, 9.5f, TEXTALIGN_ML);

	CUIRect SlotList, Preview;
	Body.VSplitLeft(190.0f, &SlotList, &Preview);
	SlotList.VSplitRight(8.0f, &SlotList, nullptr);

	static CScrollRegion s_BakeSlotScroll;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 20.0f;
	ScrollParams.m_ScrollbarThickness = 10.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	s_BakeSlotScroll.Begin(&SlotList, &ScrollParams);

	for(int i = 0; i < (int)vSlots.size(); ++i)
	{
		CUIRect Button;
		SlotList.HSplitTop(22.0f, &Button, &SlotList);
		SlotList.HSplitTop(3.0f, nullptr, &SlotList);
		if(!s_BakeSlotScroll.AddRect(Button))
			continue;

		char aSlotName[80];
		if(s_vBakeSlotPacks[i].empty())
			str_copy(aSlotName, vSlots[i].m_pName, sizeof(aSlotName));
		else
			str_format(aSlotName, sizeof(aSlotName), "%s *", vSlots[i].m_pName);
		if(DoButton_MenuTab(&s_aBakeSlotButtons[i], aSlotName, i == s_BakeCurrentSlot, &Button, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_BakeCurrentSlot = i;
	}
	s_BakeSlotScroll.End();

	const int CurSlot = s_BakeCurrentSlot;
	const std::vector<int> &vSprites = vSlots[CurSlot].m_vSprites;

	int SelectedIndex = 0;
	if(!s_vBakeSlotPacks[CurSlot].empty())
	{
		SelectedIndex = -1;
		for(size_t i = 0; i < m_vGameList.size(); ++i)
		{
			if(s_vBakeSlotPacks[CurSlot] == m_vGameList[i].m_aName)
			{
				SelectedIndex = (int)i + 1;
				break;
			}
		}
	}

	static CListBox s_BakePreviewList;
	const int NumItems = (int)m_vGameList.size() + 1;
	Preview.HSplitTop(18.0f, &Header, &Preview);
	char aPreviewTitle[128];
	str_format(aPreviewTitle, sizeof(aPreviewTitle), "Pick %s for this pack (click the base card to use the base pack)", vSlots[CurSlot].m_pName);
	Ui()->DoLabel(&Header, aPreviewTitle, 11.0f, TEXTALIGN_ML);

	const float Margin = 8.0f;
	const float CardWidth = 160.0f;
	const float CardHeight = 112.0f;
	const int PerRow = std::max(1, (int)(Preview.w / (CardWidth + Margin)));
	s_BakePreviewList.DoStart(CardHeight + Margin, NumItems, PerRow, 1, SelectedIndex, &Preview, false);

	const IGraphics::CTextureHandle BaseTexture = [&]() {
		for(const SCustomGame &Pack : m_vGameList)
		{
			if(str_comp(Pack.m_aName, s_BakeBasePack.c_str()) == 0)
				return Pack.m_RenderTexture;
		}
		return IGraphics::CTextureHandle();
	}();

	for(int i = 0; i < NumItems; ++i)
	{
		const SCustomGame *pPack = i == 0 ? nullptr : &m_vGameList[i - 1];
		const void *pId = i == 0 ? (const void *)&s_BakePreviewList : (const void *)pPack;
		const CListboxItem Item = s_BakePreviewList.DoNextItem(pId, SelectedIndex >= 0 && SelectedIndex == i);
		if(!Item.m_Visible)
			continue;

		CUIRect Card = Item.m_Rect;
		Card.Margin(Margin / 2.0f, &Card);
		Graphics()->DrawRect(Card.x, Card.y, Card.w, Card.h, ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 4.0f);

		CUIRect CardName, CardPreview;
		Card.HSplitTop(16.0f, &CardName, &CardPreview);
		CardPreview.Margin(4.0f, &CardPreview);

		SLabelProperties Props;
		Props.m_MaxWidth = CardName.w;
		Props.m_EllipsisAtEnd = true;
		const char *pName = i == 0 ? "Use base pack" : pPack->m_aName;
		Ui()->DoLabel(&CardName, pName, 10.0f, TEXTALIGN_MC, Props);

		if(i == 0)
			DrawBakePreview(Graphics(), BaseTexture, {}, CardPreview);
		else
			DrawBakePreview(Graphics(), pPack->m_RenderTexture, vSprites, CardPreview);
	}

	const int NewSelected = s_BakePreviewList.DoEnd();
	if(NewSelected != SelectedIndex && NewSelected >= 0 && NewSelected < NumItems)
	{
		if(NewSelected == 0)
			s_vBakeSlotPacks[CurSlot].clear();
		else
			s_vBakeSlotPacks[CurSlot] = m_vGameList[NewSelected - 1].m_aName;
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
