`timescale 1ns/1ps

module top #(
    parameter integer HALF_CYCLE_TICKS = 180,
    parameter integer PHASE_TICK_DIV = 1,
    parameter integer PHASE_MAX = 180,
    parameter integer ANGLE_STEP = 15,
    parameter integer ANGLE_DEFAULT = 90,
    parameter integer GATE_PULSE_TICKS = 4
) (
    input  wire clk,
    input  wire rst,
    input  wire btn_up,
    input  wire btn_down,
    input  wire btn_reset,
    output wire sync_square,
    output wire zero_cross_pulse,
    output wire gate_pulse,
    output wire [$clog2(PHASE_MAX + 1)-1:0] phase_count,
    output wire [$clog2(PHASE_MAX + 1)-1:0] angle_setpoint
);
    localparam integer GATE_WIDTH = $clog2(GATE_PULSE_TICKS + 1);
    /* verilator lint_off WIDTH */
    localparam [GATE_WIDTH-1:0] GATE_PULSE_COUNT = GATE_PULSE_TICKS;
    /* verilator lint_on WIDTH */
    localparam integer DIV_WIDTH = (PHASE_TICK_DIV <= 1) ? 1 : $clog2(PHASE_TICK_DIV);
    /* verilator lint_off WIDTH */
    localparam [DIV_WIDTH-1:0] DIV_LAST = PHASE_TICK_DIV - 1;
    /* verilator lint_on WIDTH */

    reg [GATE_WIDTH-1:0] gate_countdown;
    reg [DIV_WIDTH-1:0] phase_tick_count;
    reg phase_tick_enable;

    always @(posedge clk) begin
        if (rst) begin
            phase_tick_count <= 0;
            phase_tick_enable <= 1'b0;
        end else if (PHASE_TICK_DIV <= 1) begin
            phase_tick_enable <= 1'b1;
        end else if (phase_tick_count == DIV_LAST) begin
            phase_tick_count <= 0;
            phase_tick_enable <= 1'b1;
        end else begin
            phase_tick_count <= phase_tick_count + 1'b1;
            phase_tick_enable <= 1'b0;
        end
    end

    sync_generator #(
        .HALF_CYCLE_TICKS(HALF_CYCLE_TICKS)
    ) u_sync_generator (
        .clk(clk),
        .rst(rst),
        .tick_enable(phase_tick_enable),
        .sync_square(sync_square),
        .zero_cross_pulse(zero_cross_pulse)
    );

    phase_counter #(
        .PHASE_MAX(PHASE_MAX)
    ) u_phase_counter (
        .clk(clk),
        .rst(rst),
        .zero_cross_pulse(zero_cross_pulse),
        .tick_enable(phase_tick_enable),
        .phase_count(phase_count)
    );

    control_angle #(
        .PHASE_MAX(PHASE_MAX),
        .ANGLE_STEP(ANGLE_STEP),
        .ANGLE_DEFAULT(ANGLE_DEFAULT)
    ) u_control_angle (
        .clk(clk),
        .rst(rst),
        .btn_up(btn_up),
        .btn_down(btn_down),
        .btn_reset(btn_reset),
        .angle_setpoint(angle_setpoint)
    );

    assign gate_pulse = (gate_countdown != 0);

    always @(posedge clk) begin
        if (rst || zero_cross_pulse) begin
            gate_countdown <= 0;
        end else if (phase_tick_enable && phase_count == angle_setpoint) begin
            gate_countdown <= GATE_PULSE_COUNT;
        end else if (gate_countdown != 0) begin
            gate_countdown <= gate_countdown - 1'b1;
        end
    end
endmodule
