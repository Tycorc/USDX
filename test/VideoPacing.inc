{ Optional native-window measurements for VideoSmoke. They use a synthetic
  video clock, without song audio or microphone input. GPL version 2 or later. }

{$IFDEF UseMpvVideo}
// This SDL2 function is absent from the bundled Pascal declarations.
procedure PacingDrawableSize(Window: PSDL_Window; var Width, Height: SInt32);
  cdecl; external SDL_LibName name 'SDL_GL_GetDrawableSize';

procedure TestNativePacing(Seconds: integer);
var
  Provider: IVideoPlayback;
  Video: IVideo;
  Event: TSDL_Event;
  DrawableWidth, DrawableHeight: SInt32;
  SwapResult, SampleCount, Over33, Over50, Over100: integer;
  Started, PreviousSwap, NowCounter, OpenStarted, CloseStarted: UInt64;
  Frequency, Elapsed, Interval, LongTime, OpenTime, CloseTime: double;
  Intervals: array of double;
  SavedBackend: string;
  Finalized: boolean;

  procedure SortIntervals(Left, Right: integer);
  var LowIndex, HighIndex: integer; Pivot, Value: double;
  begin
    LowIndex:=Left; HighIndex:=Right; Pivot:=Intervals[(Left+Right) div 2];
    repeat
      while Intervals[LowIndex]<Pivot do Inc(LowIndex);
      while Intervals[HighIndex]>Pivot do Dec(HighIndex);
      if LowIndex<=HighIndex then
      begin
        Value:=Intervals[LowIndex]; Intervals[LowIndex]:=Intervals[HighIndex];
        Intervals[HighIndex]:=Value;
        Inc(LowIndex); Dec(HighIndex);
      end;
    until LowIndex>HighIndex;
    if Left<HighIndex then SortIntervals(Left,HighIndex);
    if LowIndex<Right then SortIntervals(LowIndex,Right);
  end;

  function Percentile(Percent: integer): double;
  begin
    // Nearest-rank percentile, with an index in [0, SampleCount-1].
    Result:=Intervals[((SampleCount*Percent+99) div 100)-1];
  end;

  procedure HandleEvents;
  begin
    while SDL_PollEvent(@Event)<>0 do
      if (Event.type_=SDL_QUITEV) or
         ((Event.type_=SDL_KEYDOWN) and (Event.key.keysym.sym=SDLK_ESCAPE)) or
         ((Event.type_=SDL_WINDOWEVENT) and (Event.window.event=SDL_WINDOWEVENT_CLOSE)) then
        raise Exception.Create('PACING INTERRUPTED: window closed or Escape pressed');
  end;

begin
  Require((Seconds>=10) and (Seconds<=120),'native pacing duration must be 10 through 120 seconds');
  Provider:=FindVideoProvider('MPV_Video');
  Require(Provider<>nil,'missing MPV provider for native pacing');
  Frequency:=SDL_GetPerformanceFrequency;
  Require(Frequency>0,'no performance-counter frequency for native pacing');
  SavedBackend:=Ini.VideoBackend;
  Ini.VideoBackend:='mpv';
  Video:=nil;
  Finalized:=false;
  try
    SDL_SetWindowSize(Screen,1280,720);
    SDL_ShowWindow(Screen);
    SDL_PumpEvents;
    PacingDrawableSize(Screen,DrawableWidth,DrawableHeight);
    Require((DrawableWidth>0) and (DrawableHeight>0),'native window has no drawable area');
    RenderW:=1280; RenderH:=720;
    ScreenW:=DrawableWidth; ScreenH:=DrawableHeight; ScreenWPerScreen:=DrawableWidth;
    VertexW:=DrawableWidth/1280; VertexH:=DrawableHeight/720;
    Screens:=1; ScreenAct:=1;
    Renderer.SetOrthographicProjection(0,1280,720,0,-10,10);
    Renderer.SetViewPort(0,0,DrawableWidth,DrawableHeight);
    Renderer.DepthTest:=false; Renderer.Blend:=false; Renderer.ScissorTest:=false;
    SwapResult:=SDL_GL_SetSwapInterval(1);
    ConsoleWriteLn('PACING scope: synthetic video clock; no song audio/microphone');
    ConsoleWriteLn('PACING timing: intervals between swap-loop completions, not display presentation timestamps');
    ConsoleWriteLn('PACING GL renderer: '+string(glGetString(GL_RENDERER)));
    ConsoleWriteLn('PACING GL version: '+string(glGetString(GL_VERSION)));
    ConsoleWriteLn(Format('PACING drawable %dx%d; requested vsync 1, set result %d, actual interval %d',
      [DrawableWidth,DrawableHeight,SwapResult,SDL_GL_GetSwapInterval]));
    if SwapResult<>0 then ConsoleWriteLn('PACING vsync request: '+string(SDL_GetError()));
    ConsoleWriteLn('PACING configured hardware decoding: '+Ini.VideoHardwareDecoding);

    Require(Provider.Init,'cannot initialize MPV for native pacing');
    OpenStarted:=SDL_GetPerformanceCounter;
    Video:=Provider.Open(Path(ParamStr(2)));
    OpenTime:=(SDL_GetPerformanceCounter-OpenStarted)*1000/Frequency;
    Require(VideoClass(Video)='TVideo_MPV','native pacing opened '+VideoClass(Video));
    Video.SetScreen(1);
    Video.SetScreenPosition(0,0,0);
    Video.Width:=1280; Video.Height:=720;
    Video.AspectCorrection:=acoLetterBox;
    Video.Alpha:=1; Video.Reflection:=false;
    Video.SetLoop(true);
    Video.Play;
    SampleCount:=0; Over33:=0; Over50:=0; Over100:=0; LongTime:=0;
    SetLength(Intervals,8192);
    HandleEvents;
    Started:=SDL_GetPerformanceCounter;
    PreviousSwap:=0;
    repeat
      HandleEvents;
      NowCounter:=SDL_GetPerformanceCounter;
      Video.GetFrame((NowCounter-Started)/Frequency);
      Require(not Video.Finished,'MPV finished unexpectedly during native looping playback');
      Renderer.SetClearColor(0,0,0,1);
      Renderer.ClearFrameBuffer(CLEAR_COLOR);
      Video.Draw;
      CheckError('native pacing draw');
      SDL_GL_SwapWindow(Screen);
      NowCounter:=SDL_GetPerformanceCounter;
      if PreviousSwap<>0 then
      begin
        Interval:=(NowCounter-PreviousSwap)*1000/Frequency;
        if SampleCount=Length(Intervals) then SetLength(Intervals,Length(Intervals)*2);
        Intervals[SampleCount]:=Interval;
        Inc(SampleCount);
        if Interval>1000/30 then Inc(Over33);
        if Interval>50 then begin Inc(Over50); LongTime:=LongTime+Interval; end;
        if Interval>100 then Inc(Over100);
      end;
      PreviousSwap:=NowCounter;
      Elapsed:=(NowCounter-Started)/Frequency;
    until Elapsed>=Seconds;
    Require(not Video.Finished,'MPV finished before native pacing teardown');
    CheckError('native pacing completion');
    Require(SampleCount>0,'native pacing produced no samples');
    SortIntervals(0,SampleCount-1);
    ConsoleWriteLn(Format('PACING samples %d; duration %.3f s; swap-loop ms p50 %.3f p95 %.3f p99 %.3f max %.3f',
      [SampleCount,Elapsed,Percentile(50),Percentile(95),Percentile(99),Intervals[SampleCount-1]]));
    ConsoleWriteLn(Format('PACING long intervals: over 33.333 ms %d; over 50 ms %d (%.3f ms total); over 100 ms %d',
      [Over33,Over50,LongTime,Over100]));
    CloseStarted:=SDL_GetPerformanceCounter;
    Video:=nil;
    Provider.Finalize;
    Finalized:=true;
    CloseTime:=(SDL_GetPerformanceCounter-CloseStarted)*1000/Frequency;
    CheckError('native pacing teardown');
    ConsoleWriteLn(Format('PACING open %.3f ms; close and finalize %.3f ms',[OpenTime,CloseTime]));
    ConsoleWriteLn('PASS native pacing measurements completed; timing values have no pass threshold');
  finally
    Video:=nil;
    if not Finalized then Provider.Finalize;
    Ini.VideoBackend:=SavedBackend;
  end;
end;
{$ENDIF}
