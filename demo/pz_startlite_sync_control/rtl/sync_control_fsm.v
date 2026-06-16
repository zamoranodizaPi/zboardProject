`timescale 1ns / 1ps

module sync_control_fsm #(
  parameter integer CLK_HZ = 50_000_000,
  parameter integer STARTING_MS = 2000,
  parameter integer ACCEL_MS = 11000,
  parameter integer FIELD_MS = 1200,
  parameter integer VERIFY_MS = 1800,
  parameter integer LOCKOUT_MS = 5000
) (
  input wire clk,
  input wire rst,
  input wire sample_valid,
  input wire signal_present,
  input wire freq_ok,
  input wire pll_locked,
  input wire start_cmd,
  input wire stop_cmd,
  input wire reset_cmd,
  input wire thermal_ok,
  output reg [3:0] state = 4'd0,
  output reg motor_run = 1'b0,
  output reg field_enable = 1'b0,
  output reg fault_out = 1'b0,
  output reg relay_56k = 1'b0,
  output reg relay_fax = 1'b0,
  output reg [9:0] field_duty = 10'd0
);

  localparam [3:0] IDLE = 4'd0;
  localparam [3:0] READY = 4'd1;
  localparam [3:0] STARTING = 4'd2;
  localparam [3:0] ACCEL = 4'd3;
  localparam [3:0] FIELD = 4'd4;
  localparam [3:0] VERIFY = 4'd5;
  localparam [3:0] RUNNING = 4'd6;
  localparam [3:0] FAULT = 4'd7;
  localparam [3:0] LOCKOUT = 4'd8;

  localparam integer MS_TICKS = CLK_HZ / 1000;

  reg [31:0] ms_div = 32'd0;
  reg ms_tick = 1'b0;
  reg [31:0] elapsed_ms = 32'd0;
  reg start_meta = 1'b0, start_sync = 1'b0, start_prev = 1'b0;
  reg stop_meta = 1'b0, stop_sync = 1'b0;
  reg reset_meta = 1'b0, reset_sync = 1'b0;
  reg thermal_meta = 1'b1, thermal_sync = 1'b1;

  wire start_edge = start_sync & ~start_prev;
  wire permissives_ok = signal_present && freq_ok && pll_locked && thermal_sync;

  always @(posedge clk) begin
    start_meta <= start_cmd;
    start_sync <= start_meta;
    start_prev <= start_sync;
    stop_meta <= stop_cmd;
    stop_sync <= stop_meta;
    reset_meta <= reset_cmd;
    reset_sync <= reset_meta;
    thermal_meta <= thermal_ok;
    thermal_sync <= thermal_meta;

    if (ms_div == MS_TICKS - 1) begin
      ms_div <= 32'd0;
      ms_tick <= 1'b1;
    end else begin
      ms_div <= ms_div + 32'd1;
      ms_tick <= 1'b0;
    end
  end

  always @(posedge clk) begin
    if (rst) begin
      state <= IDLE;
      elapsed_ms <= 32'd0;
    end else begin
      if (ms_tick) elapsed_ms <= elapsed_ms + 32'd1;

      if (!thermal_sync && state != IDLE && state != FAULT && state != LOCKOUT) begin
        state <= FAULT;
        elapsed_ms <= 32'd0;
      end else if (stop_sync && state != FAULT && state != LOCKOUT) begin
        state <= IDLE;
        elapsed_ms <= 32'd0;
      end else begin
        case (state)
          IDLE: begin
            if (permissives_ok) begin
              state <= READY;
              elapsed_ms <= 32'd0;
            end
          end
          READY: begin
            if (!permissives_ok) begin
              state <= IDLE;
              elapsed_ms <= 32'd0;
            end else if (start_edge) begin
              state <= STARTING;
              elapsed_ms <= 32'd0;
            end
          end
          STARTING: if (elapsed_ms >= STARTING_MS) begin
            state <= ACCEL;
            elapsed_ms <= 32'd0;
          end
          ACCEL: if (elapsed_ms >= ACCEL_MS) begin
            state <= FIELD;
            elapsed_ms <= 32'd0;
          end
          FIELD: if (elapsed_ms >= FIELD_MS) begin
            state <= VERIFY;
            elapsed_ms <= 32'd0;
          end
          VERIFY: if (elapsed_ms >= VERIFY_MS) begin
            state <= RUNNING;
            elapsed_ms <= 32'd0;
          end
          RUNNING: begin
            if (!permissives_ok) begin
              state <= FAULT;
              elapsed_ms <= 32'd0;
            end
          end
          FAULT: if (reset_sync) begin
            state <= LOCKOUT;
            elapsed_ms <= 32'd0;
          end
          LOCKOUT: if (elapsed_ms >= LOCKOUT_MS) begin
            state <= IDLE;
            elapsed_ms <= 32'd0;
          end
          default: begin
            state <= IDLE;
            elapsed_ms <= 32'd0;
          end
        endcase
      end
    end
  end

  always @(*) begin
    motor_run = (state == STARTING || state == ACCEL || state == FIELD || state == VERIFY || state == RUNNING);
    field_enable = (state == FIELD || state == VERIFY || state == RUNNING);
    fault_out = (state == FAULT || state == LOCKOUT);
    relay_56k = (state == READY);
    relay_fax = (state == RUNNING);

    case (state)
      FIELD: field_duty = 10'd420;
      VERIFY: field_duty = 10'd720;
      RUNNING: field_duty = 10'd760;
      default: field_duty = 10'd0;
    endcase
  end

endmodule
