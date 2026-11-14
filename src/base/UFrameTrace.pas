{ Fork-only diagnostics, not for upstream.

  With USCE_FRAME_TRACE=<file> the game writes one CSV row per frame: how long
  the limiter, the drawing and the buffer swap took, the video time the sing
  screen requested, the frame number read back from the picture, and the
  timing of the microphone callbacks.

  USCE_FRAME_PIXELS=1 enables the readback. It reads one pixel row behind the
  video and so flushes the pipeline every frame; pacing runs leave it off.
  The test video carries its frame number in 16 cells along its top edge:
  cells 0 to 13 in binary, cell 14 white, cell 15 black.

  USCE_FRAME_SCRIPT="12:pause;15:pause;30:seek+5;40:seek-5;50:seek=120;60:quit"
  runs actions at the given seconds after the song started. }
unit UFrameTrace;

interface

{$IFDEF FPC}
  {$MODE Delphi}
{$ENDIF}

const
  ftaPause = 1;
  ftaSeekRelative = 2;
  ftaSeekAbsolute = 3;
  ftaRestart = 4;
  ftaQuit = 5;

function FrameTraceEnabled: boolean;
{ Main loop: before the limiter, after it, after drawing, after the swap. }
procedure FrameTraceStartFrame;
procedure FrameTraceWaited;
procedure FrameTraceDrawn;
procedure FrameTraceSwapped;
{ Sing screen, right after the video was drawn. }
procedure FrameTraceVideo(RequestedTime, SongTime, GetFrameMs: double; Paused: boolean);
{ Sing screen: the next scripted action that is due, if any. }
function FrameTraceNextAction(out Action: integer; out Value: double): boolean;
{ Capture thread: one call per microphone callback. }
procedure FrameTraceCapture;

implementation

uses
  SysUtils,
  Classes,
  sdl2,
  dglOpenGL;

type
  TScriptStep = record
    Time: double;
    Action: integer;
    Value: double;
    Done: boolean;
  end;

var
  Enabled, Pixels, HaveVideo, VideoPaused: boolean;
  Output: TextFile;
  Buffer: array[0..65535] of byte;
  Frequency: double;
  TraceStart, FrameStart, Waited, Drawn: UInt64;
  SongStart: UInt64;
  FrameNumber, VideoFrame: integer;
  VideoRequested, VideoSong, VideoGetFrameMs: double;
  Notes: string;
  Script: array of TScriptStep;
  { Written by the capture thread, read and reset by the main thread. The
    values are single machine words, so a torn read cannot happen. }
  CaptureCount: longint;
  CaptureLast, CaptureMaxGap: UInt64;

{ Till may lie before From: the capture thread writes its timestamp while the
  main thread reads its own. Unsigned subtraction would overflow then. }
function Ms(From, Till: UInt64): double;
begin
  if Till >= From then
    Result := (Till - From) * 1000 / Frequency
  else
    Result := -((From - Till) * 1000 / Frequency);
end;

function FrameTraceEnabled: boolean;
begin
  Result := Enabled;
end;

procedure FrameTraceStartFrame;
begin
  if Enabled then
    FrameStart := SDL_GetPerformanceCounter;
end;

procedure FrameTraceWaited;
begin
  if Enabled then
    Waited := SDL_GetPerformanceCounter;
end;

procedure FrameTraceDrawn;
begin
  if Enabled then
    Drawn := SDL_GetPerformanceCounter;
end;

function ReadFrameNumber: integer;
var
  viewport: array[0..3] of GLint;
  row: array of byte;
  cell, value, x: integer;

  function Bright(Index: integer): boolean;
  begin
    x := (viewport[2] * (2 * Index + 1)) div 32;
    Result := integer(row[4 * x]) + row[4 * x + 1] + row[4 * x + 2] > 384;
  end;

begin
  Result := -1;
  glGetIntegerv(GL_VIEWPORT, @viewport[0]);
  if (viewport[2] < 64) or (viewport[3] < 36) then
    Exit;
  SetLength(row, 4 * viewport[2]);
  glReadPixels(viewport[0], viewport[1] + viewport[3] - viewport[3] div 18,
    viewport[2], 1, GL_RGBA, GL_UNSIGNED_BYTE, @row[0]);
  if not Bright(14) or Bright(15) then
    Exit;
  value := 0;
  for cell := 0 to 13 do
    if Bright(cell) then
      value := value or (1 shl cell);
  Result := value;
end;

procedure FrameTraceVideo(RequestedTime, SongTime, GetFrameMs: double; Paused: boolean);
begin
  if not Enabled then
    Exit;
  HaveVideo := true;
  VideoRequested := RequestedTime;
  VideoSong := SongTime;
  VideoGetFrameMs := GetFrameMs;
  VideoPaused := Paused;
  if SongStart = 0 then
    SongStart := SDL_GetPerformanceCounter;
  if Pixels then
    VideoFrame := ReadFrameNumber
  else
    VideoFrame := -2;
end;

function FrameTraceNextAction(out Action: integer; out Value: double): boolean;
var
  i: integer;
  elapsed: double;
begin
  Result := false;
  Action := 0;
  Value := 0;
  if not Enabled or (SongStart = 0) then
    Exit;
  elapsed := Ms(SongStart, SDL_GetPerformanceCounter) / 1000;
  for i := 0 to High(Script) do
    if not Script[i].Done and (elapsed >= Script[i].Time) then
    begin
      Script[i].Done := true;
      Action := Script[i].Action;
      Value := Script[i].Value;
      Notes := Notes + Format('action%d(%.3f) ', [Action, Value]);
      Exit(true);
    end;
end;

procedure FrameTraceCapture;
var
  now: UInt64;
begin
  if not Enabled then
    Exit;
  now := SDL_GetPerformanceCounter;
  if (CaptureLast <> 0) and (now > CaptureLast) and (now - CaptureLast > CaptureMaxGap) then
    CaptureMaxGap := now - CaptureLast;
  CaptureLast := now;
  InterlockedIncrement(CaptureCount);
end;

procedure FrameTraceSwapped;
var
  swapped, last, gap: UInt64;
  count: longint;
  age: double;
  video: string;
  format: TFormatSettings;
begin
  if not Enabled then
    Exit;
  swapped := SDL_GetPerformanceCounter;
  format := DefaultFormatSettings;
  format.DecimalSeparator := '.';
  count := InterlockedExchange(CaptureCount, 0);
  gap := CaptureMaxGap;
  CaptureMaxGap := 0;
  last := CaptureLast;
  if last = 0 then
    age := -1
  else if last >= swapped then
    age := 0
  else
    age := Ms(last, swapped);
  if HaveVideo then
    video := SysUtils.Format('%.6f,%.6f,%.3f,%d,%d',
      [VideoRequested, VideoSong, VideoGetFrameMs, VideoFrame, Ord(VideoPaused)], format)
  else
    video := ',,,,';
  WriteLn(Output, SysUtils.Format('%d,%.3f,%.3f,%.3f,%.3f,%s,%d,%.3f,%.3f,%s',
    [FrameNumber, Ms(TraceStart, FrameStart), Ms(FrameStart, Waited), Ms(Waited, Drawn),
     Ms(Drawn, swapped), video, count, gap * 1000 / Frequency, age, Trim(Notes)], format));
  Inc(FrameNumber);
  HaveVideo := false;
  Notes := '';
  if FrameNumber mod 240 = 0 then
    Flush(Output);
end;

procedure ReadScript(const Text: string);
var
  steps: TStringList;
  i, colon: integer;
  item, what: string;
  step: TScriptStep;
  format: TFormatSettings;
begin
  format := DefaultFormatSettings;
  format.DecimalSeparator := '.';
  steps := TStringList.Create;
  try
    steps.Delimiter := ';';
    steps.StrictDelimiter := true;
    steps.DelimitedText := Text;
    for i := 0 to steps.Count - 1 do
    begin
      item := Trim(steps[i]);
      colon := Pos(':', item);
      if colon = 0 then
        Continue;
      step.Time := StrToFloat(Copy(item, 1, colon - 1), format);
      what := Copy(item, colon + 1, MaxInt);
      step.Value := 0;
      step.Done := false;
      if what = 'pause' then
        step.Action := ftaPause
      else if what = 'restart' then
        step.Action := ftaRestart
      else if what = 'quit' then
        step.Action := ftaQuit
      else if Copy(what, 1, 5) = 'seek=' then
      begin
        step.Action := ftaSeekAbsolute;
        step.Value := StrToFloat(Copy(what, 6, MaxInt), format);
      end
      else if Copy(what, 1, 4) = 'seek' then
      begin
        step.Action := ftaSeekRelative;
        step.Value := StrToFloat(Copy(what, 5, MaxInt), format);
      end
      else
        Continue;
      SetLength(Script, Length(Script) + 1);
      Script[High(Script)] := step;
    end;
  finally
    steps.Free;
  end;
end;

procedure Start;
var
  name: string;
begin
  name := GetEnvironmentVariable('USCE_FRAME_TRACE');
  if name = '' then
    Exit;
  Frequency := SDL_GetPerformanceFrequency;
  AssignFile(Output, name);
  SetTextBuf(Output, Buffer);
  Rewrite(Output);
  WriteLn(Output, 'frame,t_ms,wait_ms,draw_ms,swap_ms,video_req_s,song_s,getframe_ms,' +
    'video_frame,paused,capture_callbacks,capture_max_gap_ms,capture_age_ms,notes');
  Pixels := GetEnvironmentVariable('USCE_FRAME_PIXELS') <> '';
  ReadScript(GetEnvironmentVariable('USCE_FRAME_SCRIPT'));
  TraceStart := SDL_GetPerformanceCounter;
  FrameStart := TraceStart;
  Waited := TraceStart;
  Drawn := TraceStart;
  Enabled := true;
end;

initialization
  Start;

finalization
  if Enabled then
  begin
    Enabled := false;
    CloseFile(Output);
  end;

end.
