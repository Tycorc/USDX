{ Pixel captures of the real FFmpeg backend, using only the video API shared
  by main and the optional MPV branch. run-video-compare.py supplies each
  checkout's own application unit list. GPL version 2 or later. }

const
  CaptureWidth = 640;
  CaptureHeight = 240;
type
  TCapture = array[0..CaptureWidth*CaptureHeight*3-1] of byte;
var
  CompareContext: TSDL_GLContext;
  CompareProvider: IVideoPlayback;
  CompareVideo: IVideo;
  CompareMode, CompareDirectory: string;

procedure CheckCompare(Condition: boolean; const MessageText: string);
begin
  if not Condition then raise Exception.Create(MessageText);
end;

procedure ReadCapture(out Pixels: TCapture);
var RGBA: array[0..CaptureWidth*CaptureHeight*4-1] of byte; I: integer;
begin
  // RGBA/unsigned-byte is supported for readback on every GLES profile.
  glReadPixels(0,0,CaptureWidth,CaptureHeight,GL_RGBA,GL_UNSIGNED_BYTE,@RGBA[0]);
  CheckCompare(glGetError()=GL_NO_ERROR,'GL error capturing pixels');
  for I:=0 to CaptureWidth*CaptureHeight-1 do
  begin
    Pixels[I*3]:=RGBA[I*4];
    Pixels[I*3+1]:=RGBA[I*4+1];
    Pixels[I*3+2]:=RGBA[I*4+2];
  end;
end;

procedure DrawCompare;
begin
  Renderer.ScissorTest:=false;
  Renderer.SetClearColor(0.2,0.3,0.4,1);
  Renderer.ClearFrameBuffer(CLEAR_COLOR);
  CompareVideo.Draw;
end;

procedure SaveCapture(const Name: string);
var Pixels: TCapture; OutputFile: file; Header: AnsiString; Row: integer;
begin
  DrawCompare;
  ReadCapture(Pixels);
  AssignFile(OutputFile,IncludeTrailingPathDelimiter(CompareDirectory)+Name+'.ppm');
  Rewrite(OutputFile,1);
  try
    Header:='P6'+#10+IntToStr(CaptureWidth)+' '+IntToStr(CaptureHeight)+#10+'255'+#10;
    BlockWrite(OutputFile,Header[1],Length(Header));
    // OpenGL reads bottom-up; PPM rows are top-down.
    for Row:=CaptureHeight-1 downto 0 do
      BlockWrite(OutputFile,Pixels[Row*CaptureWidth*3],CaptureWidth*3);
  finally
    CloseFile(OutputFile);
  end;
  ConsoleWriteLn('CAPTURE '+Name);
end;

procedure DefaultPresentation;
var Range: TRectCoords;
begin
  Renderer.SetViewPort(0,0,320,240);
  VertexW:=320/800; VertexH:=240/600;
  ScreenAct:=1;
  CompareVideo.SetScreen(1);
  CompareVideo.SetScreenPosition(0,0,0);
  CompareVideo.Width:=RenderW; CompareVideo.Height:=RenderH;
  CompareVideo.Alpha:=1;
  CompareVideo.Reflection:=false;
  CompareVideo.ReflectionSpacing:=0;
  CompareVideo.AspectCorrection:=acoLetterBox;
  Range.Left:=0; Range.Right:=1; Range.Upper:=0; Range.Lower:=1;
  CompareVideo.SetFrameRange(Range);
end;

procedure WaitForFrame;
var Started: cardinal; Pixels: TCapture; I, Colored: integer;
begin
  Started:=SDL_GetTicks;
  repeat
    CompareVideo.GetFrame(0);
    DrawCompare;
    ReadCapture(Pixels);
    Colored:=0;
    for I:=0 to CaptureWidth*CaptureHeight-1 do
      if (Abs(integer(Pixels[I*3])-integer(Pixels[I*3+1]))>90) or
         (Abs(integer(Pixels[I*3+1])-integer(Pixels[I*3+2]))>90) then Inc(Colored);
    // The generated fixture has saturated quadrants. This rejects both an
    // undecoded black frame and the grey-blue clear color without a fixed wait.
    if Colored>1000 then Exit;
    SDL_PumpEvents;
    SDL_Delay(10);
  until SDL_GetTicks-Started>5000;
  raise Exception.Create('No decoded FFmpeg frame within five seconds');
end;

procedure CaptureCases(const FileName, Prefix: string; InMemory: boolean);
var Media: TBytes; InputFile: file; Range: TRectCoords;
begin
  Media:=nil;
  if InMemory then
  begin
    AssignFile(InputFile,FileName);
    Reset(InputFile,1);
    try
      SetLength(Media,FileSize(InputFile));
      if Length(Media)>0 then BlockRead(InputFile,Media[0],Length(Media));
    finally
      CloseFile(InputFile);
    end;
  end;
  CompareVideo:=CompareProvider.Open(Path(FileName),Media);
  CheckCompare(CompareVideo<>nil,'Cannot open '+FileName);
  DefaultPresentation;
  WaitForFrame;

  // UScreenSingController selects BackgroundAspectCorrection, and
  // UScreenSingView draws the video's default fullscreen geometry.
  SaveCapture(Prefix+'-sing-letterbox');
  CompareVideo.AspectCorrection:=acoCrop;
  SaveCapture(Prefix+'-sing-crop');
  CompareVideo.AspectCorrection:=acoHalfway;
  SaveCapture(Prefix+'-sing-halfway');

  // UScreenSong's selected cover: Modern.ini SongListCover geometry,
  // crop, configurable reflection and fade alpha. No theme assets needed.
  DefaultPresentation;
  CompareVideo.SetScreenPosition(525,340,1);
  CompareVideo.Width:=215; CompareVideo.Height:=190;
  CompareVideo.AspectCorrection:=acoCrop;
  SaveCapture(Prefix+'-preview-cover');
  CompareVideo.Alpha:=0.45;
  SaveCapture(Prefix+'-preview-fade');
  CompareVideo.Alpha:=1;
  CompareVideo.Reflection:=true;
  CompareVideo.ReflectionSpacing:=13;
  SaveCapture(Prefix+'-preview-reflection');
  // Positive spacing is clipped by the video's box on both revisions.
  // The public spacing setter also permits an overlap: exercise visible
  // reflection pixels inside that box without changing production drawing.
  CompareVideo.ReflectionSpacing:=-190;
  SaveCapture(Prefix+'-reflection-overlap');
  // CoverFull explicitly requests fullscreen letterbox and no reflection.
  DefaultPresentation;
  CompareVideo.SetScreenPosition(0,0,1);
  SaveCapture(Prefix+'-preview-full');

  // Exercise bounds and frame-range setters beyond the default theme layout.
  CompareVideo.SetScreenPosition(-50,80,1);
  CompareVideo.Width:=280; CompareVideo.Height:=260;
  CompareVideo.AspectCorrection:=acoCrop;
  SaveCapture(Prefix+'-clipped');
  Range.Left:=0.1; Range.Right:=0.8; Range.Upper:=0.2; Range.Lower:=0.9;
  CompareVideo.SetFrameRange(Range);
  SaveCapture(Prefix+'-frame-range');
  DefaultPresentation;
  Renderer.SetViewPort(320,0,320,240);
  ScreenAct:=2; CompareVideo.SetScreen(2);
  CompareVideo.AspectCorrection:=acoCrop;
  SaveCapture(Prefix+'-second-screen');
  DefaultPresentation;
  Renderer.SetViewPort(0,0,320,180);
  VertexH:=180/600;
  SaveCapture(Prefix+'-wide-display');
  CompareVideo:=nil;
end;

procedure RunCompare;
var Major, Minor, Profile, I: integer; Candidate: IVideoPlayback;
begin
  SetDefaultNumericLocale;
  Log.SetLogLevel(LOG_LEVEL_INFO);
  Ini:=TIni.Create;
  SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING,'1');
  CheckCompare(SDL_Init(SDL_INIT_VIDEO or SDL_INIT_TIMER)=0,string(SDL_GetError()));
  CompareMode:=ParamStr(1); CompareDirectory:=ParamStr(4);
  if CompareMode='gles2' then begin Major:=2; Minor:=0; Profile:=SDL_GL_CONTEXT_PROFILE_ES; end
  else if CompareMode='gles3' then begin Major:=3; Minor:=0; Profile:=SDL_GL_CONTEXT_PROFILE_ES; end
  else if CompareMode='gl2' then begin Major:=2; Minor:=1; Profile:=SDL_GL_CONTEXT_PROFILE_COMPATIBILITY; end
  else begin Major:=3; Minor:=3; Profile:=SDL_GL_CONTEXT_PROFILE_CORE; end;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,Major);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,Minor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,Profile);
  Screen:=SDL_CreateWindow('FFmpeg comparison',0,0,CaptureWidth,CaptureHeight,
    SDL_WINDOW_OPENGL or SDL_WINDOW_HIDDEN);
  CheckCompare(Screen<>nil,'SDL window failed: '+string(SDL_GetError()));
  CompareContext:=SDL_GL_CreateContext(Screen);
  CheckCompare(CompareContext<>nil,'SDL context failed: '+string(SDL_GetError()));
  RenderW:=800; RenderH:=600; ScreenW:=CaptureWidth; ScreenH:=CaptureHeight;
  ScreenWPerScreen:=320; Screens:=2; ScreenAct:=1;
  if Profile=SDL_GL_CONTEXT_PROFILE_ES then Renderer:=TRenderer_OpenGLES.Create(CompareContext,Major,Minor)
  else if Major>=3 then Renderer:=TRenderer_OpenGL3.Create(CompareContext,Major,Minor)
  else Renderer:=TRenderer_OpenGL2.Create(CompareContext,Major,Minor);
  Renderer.SetOrthographicProjection(0,800,600,0,-10,10);
  Renderer.DepthTest:=false; Renderer.Blend:=true; Renderer.ScissorTest:=false;
  for I:=0 to MediaManager.Count-1 do
    if Supports(MediaManager[I],IVideoPlayback,Candidate) and
       (Candidate.GetName='FFmpeg_Video') then CompareProvider:=Candidate;
  CheckCompare(CompareProvider<>nil,'Missing FFmpeg provider');
  CheckCompare(CompareProvider.Init,'FFmpeg initialization failed');
  CaptureCases(ParamStr(2),'landscape-file',false);
  CaptureCases(ParamStr(2),'landscape-memory',true);
  CaptureCases(ParamStr(3),'portrait-file',false);
  CaptureCases(ParamStr(3),'portrait-memory',true);
  CompareProvider.Finalize; CompareProvider:=nil;
  Renderer.Free; Renderer:=nil;
  SDL_DestroyWindow(Screen); Screen:=nil; SDL_Quit;
  Ini.Free; Ini:=nil;
end;
begin
  try RunCompare;
  except on E: Exception do begin ConsoleWriteLn(E.ClassName+': '+E.Message); Halt(1); end;
  end;
end.
