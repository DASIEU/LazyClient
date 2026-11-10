#include "editor.h"
#include "editor_actions.h"

#include <engine/keys.h>

#include <game/client/gameclient.h>
#include <game/mapitems.h>

#include <algorithm>
#include <vector>

void CEditor::FillGameTiles(EGameTileOp FillTile) const
{
	std::shared_ptr<CLayerTiles> pTileLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
	if(pTileLayer)
		pTileLayer->FillGameTiles(FillTile);
}

bool CEditor::CanFillGameTiles() const
{
	std::shared_ptr<CLayerTiles> pTileLayer = std::static_pointer_cast<CLayerTiles>(Map()->SelectedLayerType(0, LAYERTYPE_TILES));
	if(pTileLayer)
		return pTileLayer->CanFillGameTiles();
	return false;
}

void CEditor::AddQuadOrSound()
{
	std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
	if(!pLayer)
		return;
	if(pLayer->m_Type != LAYERTYPE_QUADS && pLayer->m_Type != LAYERTYPE_SOUNDS)
		return;

	std::shared_ptr<CLayerGroup> pGroup = Map()->SelectedGroup();

	CScreenRect GroupRect = pGroup->Mapping();
	int x = GroupRect.m_TopLeft.x + GroupRect.Width() / 2;
	int y = GroupRect.m_TopLeft.y + GroupRect.Height() / 2;
	if(m_Dialog == DIALOG_NONE && CLineInput::GetActiveInput() == nullptr && Input()->KeyPress(KEY_Q) && Input()->ModifierIsPressed())
	{
		x += MapView()->MouseWorldPos().x - (MapView()->GetWorldOffset().x * pGroup->m_ParallaxX / 100) - pGroup->m_OffsetX;
		y += MapView()->MouseWorldPos().y - (MapView()->GetWorldOffset().y * pGroup->m_ParallaxY / 100) - pGroup->m_OffsetY;
	}

	if(pLayer->m_Type == LAYERTYPE_QUADS)
		Map()->m_EditorHistory.Execute(std::make_shared<CEditorActionNewEmptyQuad>(Map(), Map()->m_SelectedGroup, Map()->m_vSelectedLayers[0], x, y));
	else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
		Map()->m_EditorHistory.Execute(std::make_shared<CEditorActionNewEmptySound>(Map(), Map()->m_SelectedGroup, Map()->m_vSelectedLayers[0], x, y));
}

void CEditor::AddGroup()
{
	Map()->NewGroup();
	Map()->m_SelectedGroup = Map()->m_vpGroups.size() - 1;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionGroup>(Map(), Map()->m_SelectedGroup, false));
}

void CEditor::AddSoundLayer()
{
	std::shared_ptr<CLayer> pSoundLayer = std::make_shared<CLayerSounds>(Map());
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pSoundLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->m_Collapse = false;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddTileLayer()
{
	std::shared_ptr<CLayer> pTileLayer = std::make_shared<CLayerTiles>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pTileLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->m_Collapse = false;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

static bool IsAutoFreezeHookableGameTile(int Index)
{
	switch(Index)
	{
	case TILE_SOLID:
	case TILE_FREEZE:
	case TILE_UNFREEZE:
	case TILE_DFREEZE:
	case TILE_DUNFREEZE:
	case TILE_LFREEZE:
	case TILE_LUNFREEZE:
	case TILE_WALLJUMP:
		return true;
	default:
		return false;
	}
}

std::vector<bool> CEditor::BuildAutoFreezeMask(const CLayerTiles *pLayer) const
{
	const size_t TileCount = (size_t)pLayer->m_Width * pLayer->m_Height;
	std::vector<bool> vSource(TileCount, false);
	std::vector<bool> vOccupied(TileCount, false);

	for(int y = 0; y < pLayer->m_Height; y++)
	{
		for(int x = 0; x < pLayer->m_Width; x++)
		{
			const size_t Index = (size_t)y * pLayer->m_Width + x;
			const int TileIndex = pLayer->m_pTiles[Index].m_Index;
			vOccupied[Index] = TileIndex != TILE_AIR;
			// The game layer stores the actual hookable blocks, while a regular
			// tile layer is used as-is and every non-empty tile counts as a block.
			vSource[Index] = pLayer->m_HasGame ? IsAutoFreezeHookableGameTile(TileIndex) : TileIndex != TILE_AIR;
		}
	}

	std::vector<bool> vFreeze(TileCount, false);
	for(int y = 0; y < pLayer->m_Height; y++)
	{
		for(int x = 0; x < pLayer->m_Width; x++)
		{
			if(!vSource[(size_t)y * pLayer->m_Width + x])
				continue;
			for(int dy = -1; dy <= 1; dy++)
			{
				for(int dx = -1; dx <= 1; dx++)
				{
					if(dx == 0 && dy == 0)
						continue;
					const int nx = x + dx;
					const int ny = y + dy;
					if(nx < 0 || nx >= pLayer->m_Width || ny < 0 || ny >= pLayer->m_Height)
						continue;
					const size_t Neighbor = (size_t)ny * pLayer->m_Width + nx;
					if(!vOccupied[Neighbor])
						vFreeze[Neighbor] = true;
				}
			}
		}
	}
	return vFreeze;
}

void CEditor::AutoFreezeMap(int GroupIndex, int LayerIndex, int Image, int TileId)
{
	if(GroupIndex < 0 || GroupIndex >= (int)Map()->m_vpGroups.size())
		return;
	std::shared_ptr<CLayerGroup> pGroup = Map()->m_vpGroups[GroupIndex];
	if(LayerIndex < 0 || LayerIndex >= (int)pGroup->m_vpLayers.size())
		return;
	if(Image < 0 || Image >= (int)Map()->m_vpImages.size())
		return;

	std::shared_ptr<CLayerTiles> pSource = std::dynamic_pointer_cast<CLayerTiles>(pGroup->m_vpLayers[LayerIndex]);
	if(!pSource)
		return;

	const std::vector<bool> vFreeze = BuildAutoFreezeMask(pSource.get());

	std::shared_ptr<CLayerTiles> pLayer = std::make_shared<CLayerTiles>(Map(), pSource->m_Width, pSource->m_Height);
	pLayer->m_Image = Image;
	str_copy(pLayer->m_aName, "Auto freeze", sizeof(pLayer->m_aName));
	const int FreezeTile = std::clamp(TileId, 0, 255);
	for(int y = 0; y < pLayer->m_Height; y++)
	{
		for(int x = 0; x < pLayer->m_Width; x++)
		{
			if(vFreeze[(size_t)y * pLayer->m_Width + x])
				pLayer->SetTileIgnoreHistory(x, y, CTile{(unsigned char)FreezeTile});
		}
	}

	pGroup->AddLayer(pLayer);
	const int NewLayerIndex = (int)pGroup->m_vpLayers.size() - 1;
	Map()->SelectLayer(NewLayerIndex, GroupIndex);
	pGroup->m_Collapse = false;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), GroupIndex, NewLayerIndex));
}

void CEditor::PopupMergeLayersInvoke(const std::vector<std::shared_ptr<CLayerTiles>> &vpLayers, const std::vector<int> &vLayerIndices)
{
	SMergeLayersPopupContext *pCtx = &m_MergeLayersPopupContext;
	pCtx->m_pEditor = this;
	pCtx->m_GroupIndex = Map()->m_SelectedGroup;
	pCtx->m_OutputImage = -1;
	pCtx->m_vEntries.clear();

	for(size_t i = 0; i < vpLayers.size() && i < vLayerIndices.size(); i++)
	{
		SMergeLayersPopupContext::SEntry Entry;
		Entry.m_pLayer = vpLayers[i];
		Entry.m_LayerIndex = vLayerIndices[i];

		bool aUsed[256] = {};
		for(int t = 0; t < Entry.m_pLayer->m_Width * Entry.m_pLayer->m_Height; t++)
		{
			const int Index = Entry.m_pLayer->m_pTiles[t].m_Index;
			if(Index != TILE_AIR)
				aUsed[Index] = true;
		}
		for(int Id = 1; Id < 256; Id++)
		{
			if(aUsed[Id])
			{
				Entry.m_vTileIds.push_back(Id);
				Entry.m_vSelected.push_back(true);
			}
		}
		pCtx->m_vEntries.push_back(std::move(Entry));
	}

	// Merge in layer order so that the topmost selected layer wins when multiple
	// layers contain a selected tile at the same position.
	std::sort(pCtx->m_vEntries.begin(), pCtx->m_vEntries.end(), [](const auto &A, const auto &B) { return A.m_LayerIndex < B.m_LayerIndex; });

	if(!pCtx->m_vEntries.empty())
		pCtx->m_OutputImage = pCtx->m_vEntries.front().m_pLayer->m_Image;
	if(pCtx->m_OutputImage < 0 && Map()->m_SelectedImage >= 0 && (size_t)Map()->m_SelectedImage < Map()->m_vpImages.size())
		pCtx->m_OutputImage = Map()->m_SelectedImage;

	Ui()->DoPopupMenu(pCtx, Ui()->MouseX(), Ui()->MouseY(), 430.0f, std::min(460.0f, Ui()->Screen()->h - 20.0f), pCtx, PopupMergeLayers);
}

void CEditor::MergeLayers()
{
	SMergeLayersPopupContext &Ctx = m_MergeLayersPopupContext;
	if(Ctx.m_GroupIndex < 0 || Ctx.m_GroupIndex >= (int)Map()->m_vpGroups.size())
		return;
	if(Ctx.m_OutputImage < 0 || Ctx.m_OutputImage >= (int)Map()->m_vpImages.size())
		return;

	int Width = 2;
	int Height = 2;
	for(const auto &Entry : Ctx.m_vEntries)
	{
		Width = std::max(Width, Entry.m_pLayer->m_Width);
		Height = std::max(Height, Entry.m_pLayer->m_Height);
	}

	std::shared_ptr<CLayerGroup> pGroup = Map()->m_vpGroups[Ctx.m_GroupIndex];
	std::shared_ptr<CLayerTiles> pLayer = std::make_shared<CLayerTiles>(Map(), Width, Height);
	pLayer->m_Image = Ctx.m_OutputImage;
	str_copy(pLayer->m_aName, "Merged", sizeof(pLayer->m_aName));

	for(const auto &Entry : Ctx.m_vEntries)
	{
		// Ignore layers that were removed or moved while the popup was open.
		if(std::find(pGroup->m_vpLayers.begin(), pGroup->m_vpLayers.end(), Entry.m_pLayer) == pGroup->m_vpLayers.end())
			continue;

		bool aSelected[256] = {};
		for(size_t i = 0; i < Entry.m_vTileIds.size() && i < Entry.m_vSelected.size(); i++)
			aSelected[Entry.m_vTileIds[i]] = Entry.m_vSelected[i];

		for(int y = 0; y < Entry.m_pLayer->m_Height && y < Height; y++)
		{
			for(int x = 0; x < Entry.m_pLayer->m_Width && x < Width; x++)
			{
				const CTile Tile = Entry.m_pLayer->m_pTiles[y * Entry.m_pLayer->m_Width + x];
				if(Tile.m_Index != TILE_AIR && aSelected[Tile.m_Index])
					pLayer->SetTileIgnoreHistory(x, y, Tile);
			}
		}
	}

	pGroup->AddLayer(pLayer);
	const int NewLayerIndex = (int)pGroup->m_vpLayers.size() - 1;
	Map()->SelectLayer(NewLayerIndex, Ctx.m_GroupIndex);
	pGroup->m_Collapse = false;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Ctx.m_GroupIndex, NewLayerIndex));
}

void CEditor::AddQuadsLayer()
{
	std::shared_ptr<CLayer> pQuadLayer = std::make_shared<CLayerQuads>(Map());
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pQuadLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->m_Collapse = false;
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddSwitchLayer()
{
	std::shared_ptr<CLayer> pSwitchLayer = std::make_shared<CLayerSwitch>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->MakeSwitchLayer(pSwitchLayer);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pSwitchLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	m_pBrush->Clear();
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddFrontLayer()
{
	std::shared_ptr<CLayer> pFrontLayer = std::make_shared<CLayerFront>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->MakeFrontLayer(pFrontLayer);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pFrontLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	m_pBrush->Clear();
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddTuneLayer()
{
	std::shared_ptr<CLayer> pTuneLayer = std::make_shared<CLayerTune>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->MakeTuneLayer(pTuneLayer);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pTuneLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	m_pBrush->Clear();
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddSpeedupLayer()
{
	std::shared_ptr<CLayer> pSpeedupLayer = std::make_shared<CLayerSpeedup>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->MakeSpeedupLayer(pSpeedupLayer);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pSpeedupLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	m_pBrush->Clear();
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

void CEditor::AddTeleLayer()
{
	std::shared_ptr<CLayer> pTeleLayer = std::make_shared<CLayerTele>(Map(), Map()->m_pGameLayer->m_Width, Map()->m_pGameLayer->m_Height);
	Map()->MakeTeleLayer(pTeleLayer);
	Map()->m_vpGroups[Map()->m_SelectedGroup]->AddLayer(pTeleLayer);
	int LayerIndex = Map()->m_vpGroups[Map()->m_SelectedGroup]->m_vpLayers.size() - 1;
	Map()->SelectLayer(LayerIndex);
	m_pBrush->Clear();
	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionAddLayer>(Map(), Map()->m_SelectedGroup, LayerIndex));
}

bool CEditor::IsNonGameTileLayerSelected() const
{
	std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
	if(!pLayer)
		return false;
	if(pLayer->m_Type != LAYERTYPE_TILES)
		return false;
	if(
		pLayer == Map()->m_pGameLayer ||
		pLayer == Map()->m_pFrontLayer ||
		pLayer == Map()->m_pSwitchLayer ||
		pLayer == Map()->m_pTeleLayer ||
		pLayer == Map()->m_pSpeedupLayer ||
		pLayer == Map()->m_pTuneLayer)
		return false;

	return true;
}

void CEditor::LayerSelectImage()
{
	if(!IsNonGameTileLayerSelected())
		return;

	std::shared_ptr<CLayer> pLayer = Map()->SelectedLayer(0);
	std::shared_ptr<CLayerTiles> pTiles = std::static_pointer_cast<CLayerTiles>(pLayer);

	static SLayerPopupContext s_LayerPopupContext = {};
	s_LayerPopupContext.m_pEditor = this;
	Ui()->DoPopupMenu(&s_LayerPopupContext, Ui()->MouseX(), Ui()->MouseY(), 150, 300, &s_LayerPopupContext, PopupLayer);
	PopupSelectImageInvoke(pTiles->m_Image, Ui()->MouseX(), Ui()->MouseY());
}

void CEditor::MapDetails()
{
	const CUIRect *pScreen = Ui()->Screen();
	Map()->m_MapInfoTmp.Copy(Map()->m_MapInfo);
	static SPopupMenuId s_PopupMapInfoId;
	constexpr float PopupWidth = 400.0f;
	constexpr float PopupHeight = 170.0f;
	Ui()->DoPopupMenu(
		&s_PopupMapInfoId,
		pScreen->w / 2.0f - PopupWidth / 2.0f,
		pScreen->h / 2.0f - PopupHeight / 2.0f,
		PopupWidth,
		PopupHeight,
		this,
		PopupMapInfo);
	Ui()->SetActiveItem(nullptr);
}

void CEditor::GotoPosition()
{
	static SPopupMenuId s_PopupGotoId;
	Ui()->DoPopupMenu(&s_PopupGotoId, Ui()->MouseX(), Ui()->MouseY(), 120.0f, 52.0f, this, PopupGoto);
	Ui()->SetActiveItem(nullptr);
}

void CEditor::DeleteSelectedLayer()
{
	std::shared_ptr<CLayer> pCurrentLayer = Map()->SelectedLayer(0);
	if(!pCurrentLayer)
		return;
	if(Map()->m_pGameLayer == pCurrentLayer)
		return;

	Map()->m_EditorHistory.RecordAction(std::make_shared<CEditorActionDeleteLayer>(Map(), Map()->m_SelectedGroup, Map()->m_vSelectedLayers[0]));

	if(pCurrentLayer == Map()->m_pFrontLayer)
		Map()->m_pFrontLayer = nullptr;
	if(pCurrentLayer == Map()->m_pTeleLayer)
		Map()->m_pTeleLayer = nullptr;
	if(pCurrentLayer == Map()->m_pSpeedupLayer)
		Map()->m_pSpeedupLayer = nullptr;
	if(pCurrentLayer == Map()->m_pSwitchLayer)
		Map()->m_pSwitchLayer = nullptr;
	if(pCurrentLayer == Map()->m_pTuneLayer)
		Map()->m_pTuneLayer = nullptr;
	Map()->m_vpGroups[Map()->m_SelectedGroup]->DeleteLayer(Map()->m_vSelectedLayers[0]);

	Map()->SelectPreviousLayer();
}

void CEditor::TestMapLocally()
{
	const char *pFilenameNoMaps = str_startswith(Map()->m_aFilename, "maps/");
	if(!pFilenameNoMaps)
	{
		ShowFileDialogError("The map isn't saved in the maps/ folder. It must be saved there to load on the server.");
		return;
	}

	char aFilenameNoExt[IO_MAX_PATH_LENGTH];
	fs_split_file_extension(pFilenameNoMaps, aFilenameNoExt, sizeof(aFilenameNoExt));

	if(Client()->RconAuthed())
	{
		if(net_addr_is_local(&Client()->ServerAddress()))
		{
			OnClose();
			g_Config.m_ClEditor = 0;
			char aMapChange[IO_MAX_PATH_LENGTH + 64];
			str_format(aMapChange, sizeof(aMapChange), "change_map %s", aFilenameNoExt);
			Client()->Rcon(aMapChange);
			return;
		}
	}

	CGameClient *pGameClient = (CGameClient *)Kernel()->RequestInterface<IGameClient>();
	if(pGameClient->m_LocalServer.IsServerRunning())
	{
		m_PopupEventType = CEditor::POPEVENT_RESTART_SERVER;
		m_PopupEventActivated = true;
	}
	else
	{
		char aMapChange[IO_MAX_PATH_LENGTH + 64];
		str_format(aMapChange, sizeof(aMapChange), "change_map %s", aFilenameNoExt);
		if(pGameClient->m_LocalServer.RunServer({"sv_register 0", aMapChange}))
		{
			OnClose();
			g_Config.m_ClEditor = 0;
			Client()->Connect("localhost");
		}
		else
		{
			ShowFileDialogError("Local server could not be started.");
		}
	}
}
