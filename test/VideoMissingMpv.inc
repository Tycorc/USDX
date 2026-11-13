{ Missing-runtime coverage for an MPV-enabled build, in a fresh process.
  Distributed under the GNU General Public License, version 2 or later. }

procedure TestMissingMpv;
{$IFDEF UseMpvVideo}
var
  SavedProviders: array of IInterface;
  MpvProvider, FFmpegProvider: IVideoPlayback;
  Video: IVideo;
  Media: TBytes;
  I: integer;
begin
  ConsoleWriteLn('Testing missing libmpv fallback');
  MpvProvider:=FindVideoProvider('MPV_Video');
  FFmpegProvider:=FindVideoProvider('FFmpeg_Video');
  Require(MpvProvider<>nil,'missing-runtime test needs an MPV-enabled build');
  Require(FFmpegProvider<>nil,'missing-runtime test needs the FFmpeg provider');
  SetLength(SavedProviders,MediaManager.Count);
  for I:=0 to MediaManager.Count-1 do SavedProviders[I]:=MediaManager[I];
  try
    // Exercise the real selection path without starting unrelated visualizers.
    MediaManager.Clear;
    MediaManager.Add(MpvProvider);
    MediaManager.Add(FFmpegProvider);
    Ini.VideoBackend:='mpv';
    InitializeVideo;
    Require(VideoPlayback=MpvProvider,'missing-runtime selection did not choose the optional provider');
    // Repeated path opens must use FFmpeg; the runner also requires exactly
    // one missing-library warning across both, rather than repeated load attempts.
    for I:=1 to 2 do
    begin
      Video:=VideoPlayback.Open(Path(ParamStr(2)));
      Require(VideoClass(Video)='TVideo_FFmpeg','missing-runtime file opened '+VideoClass(Video));
      WaitFirstVideoFrame(Video,'missing-runtime file '+IntToStr(I));
      Video:=nil;
      CheckError('missing-runtime file closed');
    end;
    Media:=ReadBytes(ParamStr(2));
    Video:=VideoPlayback.Open(Path(ParamStr(2)),Media);
    Require(VideoClass(Video)='TVideo_FFmpeg','missing-runtime snapshot opened '+VideoClass(Video));
    WaitFirstVideoFrame(Video,'missing-runtime snapshot');
    Video.Play;
    PumpVideo(Video,0,1400,true);
    CheckVideoColors(1,'missing-runtime snapshot playback');
    Video:=nil;
    CheckError('missing-runtime snapshot closed');
  finally
    Video:=nil;
    MpvProvider.Finalize;
    MediaManager.Clear;
    for I:=0 to High(SavedProviders) do MediaManager.Add(SavedProviders[I]);
  end;
  ConsoleWriteLn('PASS missing libmpv fallback: repeated files and an in-memory snapshot play through FFmpeg');
end;
{$ELSE}
begin
  raise Exception.Create('missing-runtime test needs an MPV-enabled build');
end;
{$ENDIF}
