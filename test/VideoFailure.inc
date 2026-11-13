{ A real mid-stream render-target failure, not a decoder error or injected
  backend state. video_failure_fixture.py changes display width after 1.5 s.
  Distributed under the GNU General Public License, version 2 or later. }
{$IFDEF UseMpvVideo}
procedure TestMidstreamRenderFailure;
type TPixels = array[0..64*32*4-1] of byte;
var
  Provider: IVideoPlayback; Video: IVideo;
  LastFrame, CurrentFrame: TPixels;
  Started, Elapsed: cardinal;
  I, Colored, Frames: integer;

  procedure Capture(out Pixels: TPixels);
  begin
    Renderer.SetClearColor(0.5,0.5,0.5,1);
    Renderer.ClearFrameBuffer(CLEAR_COLOR);
    Video.Draw;
    glReadPixels(0,0,64,32,GL_RGBA,GL_UNSIGNED_BYTE,@Pixels[0]);
    CheckError('mid-stream failure draw');
  end;

begin
  ConsoleWriteLn('Testing real mid-stream render-target failure');
  Require(Renderer.MaxTextureSize<81920,
    'mid-stream fixture needs a maximum texture width below 81920');
  Provider:=FindVideoProvider('MPV_Video');
  Require(Provider<>nil,'missing MPV provider for mid-stream failure');
  Require(Provider.Init,'cannot initialize MPV for mid-stream failure');
  Video:=nil;
  try
    Video:=Provider.Open(Path(ParamStr(2)+'.oversize'));
    Require(VideoClass(Video)='TVideo_MPV','mid-stream fixture opened '+VideoClass(Video));
    Require(not Video.Finished,'mid-stream fixture was already finished at open');
    Capture(LastFrame);
    Colored:=0;
    for I:=0 to 64*32-1 do
      if (Abs(integer(LastFrame[I*4])-integer(LastFrame[I*4+1]))>80) or
         (Abs(integer(LastFrame[I*4+1])-integer(LastFrame[I*4+2]))>80) then Inc(Colored);
    Require(Colored>100,'mid-stream fixture has no decoded colored initial frame');
    Video.Play;
    Started:=SDL_GetTicks; Frames:=0;
    repeat
      Elapsed:=SDL_GetTicks-Started;
      Video.GetFrame(Elapsed/1000);
      if Video.Finished then Break;
      Capture(LastFrame);
      Inc(Frames);
      SDL_PumpEvents;
      SDL_Delay(10);
    until Elapsed>=5000;
    Require(Video.Finished,'mid-stream render-target failure did not finish within five seconds');
    Require(Frames>2,'mid-stream fixture failed before sustained playback');
    Capture(CurrentFrame);
    for I:=0 to High(LastFrame) do
      Require(CurrentFrame[I]=LastFrame[I],'mid-stream failure changed the last frame at byte '+IntToStr(I));
    // Calls after the terminal state must remain harmless and retain that image.
    Video.GetFrame(Elapsed/1000+1);
    Require(Video.Finished,'mid-stream failure stopped reporting Finished');
    Capture(CurrentFrame);
    for I:=0 to High(LastFrame) do
      Require(CurrentFrame[I]=LastFrame[I],'post-failure GetFrame changed the image at byte '+IntToStr(I));
  finally
    Video:=nil;
    Provider.Finalize;
  end;
  CheckError('mid-stream failure destruction');
  ConsoleWriteLn('PASS mid-stream render-target failure: Finished, last frame retained, safe drawing and destruction');
end;
{$ENDIF}
