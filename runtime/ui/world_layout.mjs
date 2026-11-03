// Explicit activation of the currently supported HUD. Original XML owns frames,
// anchors and artwork; original tutorial functions own its illustrated content.
// Pending gameplay/windows are left without click handlers, not emulated.
export const worldLayout = String.raw`
local function quiet(parent)
  for _,o in ipairs({parent:GetChildren()}) do
    local kind=o:GetObjectType();
    local tutorialParent=o;
    while tutorialParent and tutorialParent~=TutorialFrame do tutorialParent=tutorialParent:GetParent() end
    if (kind=="Button" or kind=="CheckButton") and not tutorialParent then
      for _,event in ipairs({"OnEnter","OnLeave","OnClick","OnMouseDown","OnMouseUp","PreClick","PostClick","OnDragStart","OnReceiveDrag"}) do o:SetScript(event,nil) end
    end
    quiet(o)
  end
end
quiet(UIParent)
local function hide(name) if _G[name] then _G[name]:Hide() end end
for _,name in ipairs({"PlayerRestIcon","PlayerAttackIcon","PlayerStatusTexture","BasicScriptErrors","MinimapCompassTexture","MiniMapMailFrame","MiniMapBattlefieldFrame","MiniMapTracking","MiniMapLFGFrame","MiniMapInstanceDifficulty","BonusActionBarFrame","PossessBarFrame","GeneralDockManager","ChatChannelDropDown","ChatBNPlayerDropDown"}) do hide(name) end
PlayerName:SetText(UnitName("player"));PlayerLevelText:SetText(UnitLevel("player"));
PlayerFrameManaBarText:SetText("");PlayerFrameHealthBarText:SetText("");
CharacterMicroButton:SetNormalTexture("Interface\\Buttons\\UI-MicroButtonCharacter-Up");
CharacterMicroButton:SetPushedTexture("Interface\\Buttons\\UI-MicroButtonCharacter-Down");
CharacterMicroButton:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight");
for name,texture in pairs({SpellbookMicroButton="Spellbook",TalentMicroButton="Talents",AchievementMicroButton="Achievement",QuestLogMicroButton="Quest",SocialsMicroButton="Socials",LFDMicroButton="LFG",MainMenuMicroButton="MainMenu",HelpMicroButton="Help"}) do
  if _G[name] then LoadMicroButtonTextures(_G[name],texture) end
end
PVPMicroButton:SetNormalTexture("Interface\\Buttons\\UI-MicroButtonCharacter-Up");
PVPMicroButton:SetPushedTexture("Interface\\Buttons\\UI-MicroButtonCharacter-Down");
PVPMicroButtonTexture:SetTexture("Interface\\TargetingFrame\\UI-PVP-"..UnitFactionGroup("player"));
MainMenuBarBackpackButtonIconTexture:SetAllPoints(MainMenuBarBackpackButton);
MainMenuBarBackpackButtonIconTexture:SetTexture("Interface\\Buttons\\Button-Backpack-Up");
for i=0,3 do local b=_G["CharacterBag"..i.."Slot"];local icon=_G[b:GetName().."IconTexture"];if icon then icon:Hide() end;b:Show() end
MainMenuBarBackpackButton:Show();MainMenuBarArtFrame:Show();
for i=2,10 do hide("ChatFrame"..i);hide("ChatFrame"..i.."Tab") end
ChatFrame1:Show();ChatFrame1:SetSize(430,120);
ChatFrame1:SetPoint("BOTTOMLEFT",UIParent,"BOTTOMLEFT",32,95);
ChatFrame1ButtonFrame:ClearAllPoints();
ChatFrame1ButtonFrame:SetPoint("TOPRIGHT",ChatFrame1,"TOPLEFT",-4,0);
ChatFrame1ButtonFrame:SetPoint("BOTTOMRIGHT",ChatFrame1,"BOTTOMLEFT",-4,0);
ChatFrame1ButtonFrame:SetAlpha(1);ChatFrame1ButtonFrame:Show();
-- Collapse the source's redundant fourth corner into a consistent diagonal.
for _,frame in ipairs({ChatFrame1,ChatFrame1ButtonFrame}) do
 local bg=_G[frame:GetName().."Background"];bg:ClearAllPoints();
 bg:SetPoint("TOPLEFT",frame,"TOPLEFT",-2,3);bg:SetPoint("BOTTOMRIGHT",frame,"BOTTOMRIGHT",3,-6);
end
ChatFrame1Background:SetAlpha(.25);
for _,suffix in ipairs({"UpButton","DownButton","BottomButton"}) do local b=_G["ChatFrame1ButtonFrame"..suffix];if b then b:Show() end end
ChatFrameMenuButton:Show();FriendsMicroButton:Show();
ChatFrame1Tab:ClearAllPoints();ChatFrame1Tab:SetPoint("BOTTOMLEFT",ChatFrame1,"TOPLEFT",0,0);ChatFrame1Tab:Show();
if ChatFrame1TabText then ChatFrame1TabText:SetText(GENERAL) end
ChatFrame2Tab:ClearAllPoints();ChatFrame2Tab:SetPoint("LEFT",ChatFrame1Tab,"RIGHT",4,0);ChatFrame2Tab:Show();
if ChatFrame2TabText then ChatFrame2TabText:SetText(COMBAT_LOG) end
for i=1,2 do local name="ChatFrame"..i.."Tab";local tab=_G[name];local text=_G[name.."Text"];
 text:SetWidth(0);local width=text:GetStringWidth();text:SetWidth(width);tab:SetWidth(width+32);_G[name.."Middle"]:SetWidth(width);
 for _,part in ipairs({"Left","Middle","Right"}) do _G[name.."Highlight"..part]:Hide();if i~=1 then _G[name.."Selected"..part]:Hide()end end
 tab:SetAlpha(i==1 and 1 or .6);
end
ChatFrame1EditBox:Show();ChatFrame1EditBox:EnableMouse(false);
if ChatFrame1EditBoxHeader then ChatFrame1EditBoxHeader:SetText("") end
local clock=MinimapBackdrop:CreateFontString("WorldHUDClock","OVERLAY","GameFontNormalSmall");
clock:SetPoint("TOP",Minimap,"BOTTOM",0,-3);clock:SetSize(70,14);
function WorldUIEnvironment(zone)
 MinimapZoneText:SetText(zone or "");local h,m=GetGameTime();WorldHUDClock:SetText(string.format("%d:%02d",h,m));
end
function WorldUIRefreshActions()
 MainMenuBarPageNumber:SetText(GetActionBarPage());
 for i=1,12 do
  local button=_G["ActionButton"..i];local icon=_G["ActionButton"..i.."Icon"];
  local id,kind,path=WorldHUDSlot(i);icon:SetAllPoints(button);
  if path then icon:SetTexture(path);icon:Show();button:SetNormalTexture("Interface\\Buttons\\UI-Quickslot2")
  else icon:Hide();button:SetNormalTexture("Interface\\Buttons\\UI-Quickslot") end
  _G["ActionButton"..i.."HotKey"]:SetText(i<=9 and tostring(i) or i==10 and "0" or i==11 and "-" or "=");
  _G["ActionButton"..i.."Name"]:SetText("");_G["ActionButton"..i.."Count"]:SetText("");
  button:Show();_G["ActionButton"..i.."Cooldown"]:Hide();
 end
 local forms=WorldHUDForms();
 if #forms>0 then
  local extra=math.max(0,#forms-2);local span=#forms==1 and 12 or ShapeshiftBarLeft:GetWidth()+37*extra;
  ShapeshiftBarFrame:Show();ShapeshiftBarFrame:SetWidth(span+ShapeshiftBarRight:GetWidth());
  ShapeshiftBarRight:ClearAllPoints();ShapeshiftBarRight:SetPoint("LEFT",ShapeshiftBarLeft,"LEFT",span,0);
  if extra>0 then ShapeshiftBarMiddle:SetWidth(37*extra);ShapeshiftBarMiddle:SetTexCoord(0,extra,0,1);ShapeshiftBarMiddle:Show() else ShapeshiftBarMiddle:Hide()end
 else ShapeshiftBarFrame:Hide() end
 for i=1,10 do local b=_G["ShapeshiftButton"..i];local icon=_G["ShapeshiftButton"..i.."Icon"];
  if forms[i] then icon:SetAllPoints(b);icon:SetTexture(forms[i].path);icon:Show();b:Show();b:SetChecked(forms[i].form~=0 and forms[i].form==WorldHUDActiveForm());_G[b:GetName().."Cooldown"]:Hide()
  else b:Hide() end
 end
end
ActionBarUpButton:SetScript("OnClick",function()WorldHUDPage(GetActionBarPage()%6+1);WorldUIRefreshActions()end);
ActionBarDownButton:SetScript("OnClick",function()WorldHUDPage((GetActionBarPage()+4)%6+1);WorldUIRefreshActions()end);
MinimapZoomIn:SetScript("OnClick",function()local zoom=WorldHUDZoom(1);MinimapZoomOut:Enable();if zoom==5 then MinimapZoomIn:Disable()end end);
MinimapZoomOut:SetScript("OnClick",function()local zoom=WorldHUDZoom(-1);MinimapZoomIn:Enable();if zoom==0 then MinimapZoomOut:Disable()end end);
MinimapZoomOut:Disable();
local _,class=UnitClass("player");
if class=="DEATHKNIGHT" then
 RuneFrame:Show();
 for i=1,6 do local b=_G["RuneButtonIndividual"..i];b:Show();_G[b:GetName().."Cooldown"]:Hide();_G[b:GetName().."Shine"]:Hide();
 _G[b:GetName().."Rune"]:SetTexture("Interface\\PlayerFrame\\UI-PlayerFrame-Deathknight-"..(i<=2 and "Blood" or i<=4 and "Frost" or "Unholy"));end
else RuneFrame:Hide() end
TutorialFrame_OnLoad(TutorialFrame);
TutorialFrameTextScrollFrame.scrollBarHideable=true;ScrollFrame_OnLoad(TutorialFrameTextScrollFrame);
TutorialFrame:Hide();TutorialFrameAlertButton:Hide();
local tutorialAttempted=false;local tutorialStep=1;local tutorialIds={2,3,33};
function WorldUITutorial(delta)
 tutorialStep=math.max(1,math.min(3,tutorialStep+delta));TutorialFrame:ClearAllPoints();TutorialFrame_Update(tutorialIds[tutorialStep]);
 TutorialFrameTextScrollChildFrame:SetWidth(300);TutorialFrameTextScrollChildFrame:SetHeight(TutorialFrameText:GetHeight());
 TutorialFrameTextScrollFrame:SetVerticalScroll(0);
 TutorialFrameOkayButton:SetScript("OnClick",function()TutorialFrame:Hide()end);
 if tutorialStep>1 then TutorialFramePrevButton:Enable() else TutorialFramePrevButton:Disable()end
 if tutorialStep<3 then TutorialFrameNextButton:Enable() else TutorialFrameNextButton:Disable()end
end
for _,o in ipairs({TutorialFrame:GetChildren()}) do if o:GetObjectType()=="Button" then o:SetScript("OnClick",function()TutorialFrame:Hide()end)end end
TutorialFramePrevButton:SetScript("OnClick",function()WorldUITutorial(-1)end);
TutorialFrameNextButton:SetScript("OnClick",function()WorldUITutorial(1)end);
function WorldUIRefreshHUD()
 WorldUIRefreshActions();
 if not tutorialAttempted and bit.band(WorldHUDReady(),4)~=0 then
  tutorialAttempted=true;local ok,value=pcall(GetCVar,"showTutorials");
  if UnitLevel("player")<=5 and not IsTutorialFlagged(2) and (not ok or value~="0") then WorldUITutorial(0)end
 end
end
WorldUIRefreshActions();WorldUIEnvironment("");
`;
