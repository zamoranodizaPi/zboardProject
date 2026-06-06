`timescale 1ns/1ps

module control_angle #(
    parameter integer PHASE_MAX = 180,
    parameter integer ANGLE_STEP = 10,
    parameter integer ANGLE_DEFAULT = 90
) (
    input  wire clk,
    input  wire rst,
    input  wire btn_up,
    input  wire btn_down,
    input  wire btn_reset,
    output reg  [$clog2(PHASE_MAX + 1)-1:0] angle_setpoint
);
    localparam integer ANGLE_WIDTH = $clog2(PHASE_MAX + 1);

    /* verilator lint_off WIDTH */
    localparam [ANGLE_WIDTH-1:0] MAX_ANGLE = PHASE_MAX;
    localparam [ANGLE_WIDTH-1:0] STEP_ANGLE = ANGLE_STEP;
    localparam [ANGLE_WIDTH-1:0] DEFAULT_ANGLE = ANGLE_DEFAULT;
    /* verilator lint_on WIDTH */

    reg btn_up_d;
    reg btn_down_d;
    reg btn_reset_d;

    wire up_rise = btn_up & ~btn_up_d;
    wire down_rise = btn_down & ~btn_down_d;
    wire reset_rise = btn_reset & ~btn_reset_d;

    always @(posedge clk) begin
        if (rst) begin
            btn_up_d <= 1'b0;
            btn_down_d <= 1'b0;
            btn_reset_d <= 1'b0;
            angle_setpoint <= DEFAULT_ANGLE;
        end else begin
            btn_up_d <= btn_up;
            btn_down_d <= btn_down;
            btn_reset_d <= btn_reset;

            if (reset_rise) begin
                angle_setpoint <= DEFAULT_ANGLE;
            end else if (up_rise) begin
                if (angle_setpoint + STEP_ANGLE >= MAX_ANGLE) begin
                    angle_setpoint <= MAX_ANGLE;
                end else begin
                    angle_setpoint <= angle_setpoint + STEP_ANGLE;
                end
            end else if (down_rise) begin
                if (angle_setpoint <= STEP_ANGLE) begin
                    angle_setpoint <= 0;
                end else begin
                    angle_setpoint <= angle_setpoint - STEP_ANGLE;
                end
            end
        end
    end
endmodule
