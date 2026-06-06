`timescale 1ns/1ps

module hardware_top (
    input  wire clk_100mhz,
    input  wire btn_up,
    input  wire btn_down,
    input  wire btn_reset,
    output wire led_sync,
    output wire led_gate,
    output wire led_zc,
    output wire [4:0] led_angle
);
    localparam integer CLK_HZ = 100_000_000;
    localparam integer LINE_HZ = 60;
    localparam integer PHASE_STEPS = 181;
    localparam integer PHASE_TICK_DIV = CLK_HZ / (2 * LINE_HZ * PHASE_STEPS);
    localparam integer LED_STRETCH_TICKS = CLK_HZ / 20;

    wire sync_square;
    wire zero_cross_pulse;
    wire gate_pulse;
    wire [7:0] phase_count;
    wire [7:0] angle_setpoint;

    reg btn_up_meta;
    reg btn_up_sync;
    reg btn_down_meta;
    reg btn_down_sync;
    reg btn_reset_meta;
    reg btn_reset_sync;
    reg [22:0] led_gate_countdown;

    always @(posedge clk_100mhz) begin
        btn_up_meta <= btn_up;
        btn_up_sync <= btn_up_meta;
        btn_down_meta <= btn_down;
        btn_down_sync <= btn_down_meta;
        btn_reset_meta <= btn_reset;
        btn_reset_sync <= btn_reset_meta;

        if (gate_pulse) begin
            led_gate_countdown <= LED_STRETCH_TICKS[22:0];
        end else if (led_gate_countdown != 0) begin
            led_gate_countdown <= led_gate_countdown - 1'b1;
        end
    end

    top #(
        .HALF_CYCLE_TICKS(PHASE_STEPS),
        .PHASE_TICK_DIV(PHASE_TICK_DIV),
        .PHASE_MAX(180),
        .ANGLE_STEP(15),
        .ANGLE_DEFAULT(90),
        .GATE_PULSE_TICKS(20_000)
    ) u_top (
        .clk(clk_100mhz),
        .rst(btn_reset_sync),
        .btn_up(btn_up_sync),
        .btn_down(btn_down_sync),
        .btn_reset(btn_reset_sync),
        .sync_square(sync_square),
        .zero_cross_pulse(zero_cross_pulse),
        .gate_pulse(gate_pulse),
        .phase_count(phase_count),
        .angle_setpoint(angle_setpoint)
    );

    assign led_sync = sync_square;
    assign led_gate = (led_gate_countdown != 0);
    assign led_zc = zero_cross_pulse;
    assign led_angle = angle_setpoint[7:3];
endmodule
