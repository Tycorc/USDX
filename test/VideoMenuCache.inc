{ Exercise the actual shared menu background lifecycle with real video backends.
  Distributed under the GNU General Public License, version 2 or later. }

{$IFDEF UseMpvVideo}
type
  TMenuMpvSetProperty = function(Handle: Pmpv_handle; ReplyUserdata: UInt64;
    Name: PAnsiChar; Format: longint; Data: Pointer): longint; cdecl;
var
  MenuOriginalSetProperty: TMenuMpvSetProperty;
  MenuUnpauseSubmitted: boolean;

function ObserveMenuSetProperty(Handle: Pmpv_handle; ReplyUserdata: UInt64;
  Name: PAnsiChar; Format: longint; Data: Pointer): longint; cdecl;
begin
  // Observe the real request without changing, dropping or delaying it.
  Result:=MenuOriginalSetProperty(Handle,ReplyUserdata,Name,Format,Data);
  if (Result>=0) and (Name<>nil) and (Data<>nil) and
     (string(Name)='pause') and (Format=MPV_FORMAT_FLAG) and
     (PLongint(Data)^=0) then MenuUnpauseSubmitted:=true;
end;
{$ENDIF}

procedure TestRetainedMenuVideo(const ProviderName: string);
var
  Provider: IVideoPlayback;
  {$IFDEF UseMpvVideo}
  SavedBackend: string;
  {$ENDIF}
  SavedSkin, TestSkin: TSkin;
  SavedThemePaths: IInterfaceList;
  Settings: TThemeBackground;
  FileName: IPath;
  First, Second: TMenuBackgroundVideo;

  procedure DrawMenuVideo(Menu: TMenuBackgroundVideo);
  begin
    Renderer.SetClearColor(0.5,0.5,0.5,1);
    Renderer.ClearFrameBuffer(CLEAR_COLOR);
    Menu.Draw;
    CheckError('retained menu video '+ProviderName);
  end;

  procedure PumpMenu(Menu: TMenuBackgroundVideo; Milliseconds: cardinal);
  var Started: cardinal;
  begin
    Started:=SDL_GetTicks;
    repeat
      DrawMenuVideo(Menu);
      SDL_PumpEvents;
      SDL_Delay(10);
    until SDL_GetTicks-Started>=Milliseconds;
  end;

  procedure HideAfterUnpause;
  {$IFDEF UseMpvVideo}
  var Started: cardinal;
  {$ENDIF}
  begin
    {$IFDEF UseMpvVideo}
    if ProviderName='MPV_Video' then
    begin
      Require(Assigned(mpv_set_property_async),'missing MPV property binding');
      MenuOriginalSetProperty:=mpv_set_property_async;
      MenuUnpauseSubmitted:=false;
      mpv_set_property_async:=@ObserveMenuSetProperty;
      try
        First.OnShow;
        Started:=SDL_GetTicks;
        repeat
          DrawMenuVideo(First);
          if MenuUnpauseSubmitted then Break;
          SDL_Delay(10);
        until SDL_GetTicks-Started>=250;
        Require(MenuUnpauseSubmitted,'menu did not submit an MPV unpause');
        // No intervening GetFrame can consume this request's acknowledgment:
        // the backend still has an unpause pending when the last user hides.
        First.OnFinish;
      finally
        mpv_set_property_async:=MenuOriginalSetProperty;
        MenuOriginalSetProperty:=nil;
      end;
      Exit;
    end;
    {$ENDIF}
    First.OnShow;
    PumpMenu(First,20);
    First.OnFinish;
  end;

begin
  ConsoleWriteLn('Testing retained menu video '+ProviderName);
  // TestVideoSelection has initialized the application's selector. A backend
  // setting change applies to its next Open, including menu constructors.
  Provider:=VideoPlayback;
  Require(Provider<>nil,'missing menu video provider');
  {$IFDEF UseMpvVideo}
  SavedBackend:=Ini.VideoBackend;
  if ProviderName='FFmpeg_Video' then Ini.VideoBackend:='ffmpeg'
  else Ini.VideoBackend:='mpv';
  {$ENDIF}
  Require(Provider.Init,'cannot initialize '+ProviderName+' for menu cache');
  SavedSkin:=Skin;
  SavedThemePaths:=ThemePaths;
  TestSkin:=nil; First:=nil; Second:=nil;
  ClearMenuVideoCache;
  try
    // Supply only a texture-to-file binding. No theme files or private menu
    // state are needed; both objects use the real constructor and cache.
    ThemePaths:=TInterfaceList.Create;
    TestSkin:=TSkin.Create;
    ThemePaths:=SavedThemePaths;
    Skin:=TestSkin;
    FileName:=Path(ParamStr(2));
    Skin.SetTextureEntry('VideoSmoke',FileName.GetName,FileName.GetPath);
    Settings:=DEFAULT_BACKGROUND;
    Settings.BGType:=bgtVideo;
    Settings.Tex:='VideoSmoke';
    First:=TMenuBackgroundVideo.Create(Settings);
    Second:=TMenuBackgroundVideo.Create(Settings);
    First.OnShow;
    PumpMenu(First,350);
    CheckVideoColors(0,ProviderName+' menu initial frame');
    Second.OnShow;
    PumpMenu(Second,900);
    CheckVideoColors(1,ProviderName+' acquiring another menu must not pause');
    First.OnFinish;
    PumpMenu(Second,900);
    CheckVideoColors(2,ProviderName+' releasing one menu must not pause another');

    // Retain the same cached video while every user is hidden. In particular,
    // do not call Draw/GetFrame, close the video or finalize its provider.
    Second.OnFinish;
    SDL_Delay(1100);
    First.OnShow;
    PumpMenu(First,200);
    CheckVideoColors(2,ProviderName+' hidden cached menu advanced or reopened');
    First.OnFinish;
    SDL_Delay(50);
    HideAfterUnpause;
    SDL_Delay(1100);
    First.OnShow;
    PumpMenu(First,200);
    CheckVideoColors(2,ProviderName+' immediately hidden menu advanced');
    PumpMenu(First,850);
    CheckVideoColors(3,ProviderName+' cached menu did not resume');
  finally
    First.Free;
    Second.Free;
    ClearMenuVideoCache;
    Skin:=SavedSkin;
    TestSkin.Free;
    ThemePaths:=SavedThemePaths;
    Provider.Finalize;
    {$IFDEF UseMpvVideo}
    Ini.VideoBackend:=SavedBackend;
    {$ENDIF}
  end;
  CheckError('menu cache destruction '+ProviderName);
  ConsoleWriteLn('PASS retained menu video '+ProviderName+
    ': shared users, hidden cache, immediate hide and resumed playback');
end;
