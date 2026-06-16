`timescale 1ns / 1ps

module pz_sync_control_axi_top #(
  parameter integer CLK_HZ = 50_000_000,
  parameter integer SAMPLE_RATE_HZ = 8000
) (
  input wire s_axi_aclk,
  input wire s_axi_aresetn,

  input wire [7:0] s_axi_awaddr,
  input wire s_axi_awvalid,
  output wire s_axi_awready,
  input wire [31:0] s_axi_wdata,
  input wire [3:0] s_axi_wstrb,
  input wire s_axi_wvalid,
  output wire s_axi_wready,
  output wire [1:0] s_axi_bresp,
  output wire s_axi_bvalid,
  input wire s_axi_bready,

  input wire [7:0] s_axi_araddr,
  input wire s_axi_arvalid,
  output wire s_axi_arready,
  output wire [31:0] s_axi_rdata,
  output wire [1:0] s_axi_rresp,
  output wire s_axi_rvalid,
  input wire s_axi_rready,

  input  wire ads_drdy_n,
  output wire ads_cs_n,
  output wire ads_sclk,
  output wire ads_mosi,
  input  wire ads_miso,

  output wire motor_run,
  output wire field_enable,
  output wire field_pwm,
  output wire sync_pulse,
  output wire fault_out
);

  wire rst = ~s_axi_aresetn;
  wire frame_valid;
  wire frame_bad;
  wire [15:0] status;
  wire signed [15:0] ch0, ch1, ch2, ch3, ch4, ch5, ch6, ch7;
  wire [31:0] good_frames;
  wire [31:0] bad_frames;
  wire [31:0] freq_mhz;
  wire freq_locked;
  wire zero_cross;
  wire [3:0] ctrl_state;
  wire [9:0] field_duty;
  wire signal_present;
  wire freq_ok;
  wire start_pulse;
  wire stop_pulse;
  wire reset_pulse;
  wire [31:0] control_reg;
  wire thermal_ok = control_reg[8];

  reg [31:0] abs_acc = 32'd0;
  reg [9:0] abs_count = 10'd0;
  reg [15:0] abs_mean = 16'd0;
  reg [31:0] sync_release = 32'd0;

  ads_spi_master #(
    .CLK_HZ(CLK_HZ),
    .SPI_HZ(2_000_000),
    .FRAME_WORDS(9)
  ) ads_reader (
    .clk(s_axi_aclk),
    .rst(rst),
    .drdy_n(ads_drdy_n),
    .cs_n(ads_cs_n),
    .sclk(ads_sclk),
    .mosi(ads_mosi),
    .miso(ads_miso),
    .frame_valid(frame_valid),
    .frame_bad(frame_bad),
    .status(status),
    .ch0(ch0),
    .ch1(ch1),
    .ch2(ch2),
    .ch3(ch3),
    .ch4(ch4),
    .ch5(ch5),
    .ch6(ch6),
    .ch7(ch7),
    .good_frames(good_frames),
    .bad_frames(bad_frames)
  );

  zero_cross_freq #(
    .SAMPLE_RATE_HZ(SAMPLE_RATE_HZ)
  ) freq_est (
    .clk(s_axi_aclk),
    .rst(rst),
    .sample_valid(frame_valid),
    .sample(ch0),
    .freq_mhz(freq_mhz),
    .locked(freq_locked),
    .zero_cross_pulse(zero_cross)
  );

  assign signal_present = abs_mean > 16'd1000;
  assign freq_ok = freq_mhz >= 32'd58000 && freq_mhz <= 32'd62000;

  sync_control_fsm #(
    .CLK_HZ(CLK_HZ)
  ) control (
    .clk(s_axi_aclk),
    .rst(rst),
    .sample_valid(frame_valid),
    .signal_present(signal_present),
    .freq_ok(freq_ok),
    .pll_locked(freq_locked),
    .start_cmd(start_pulse),
    .stop_cmd(stop_pulse),
    .reset_cmd(reset_pulse),
    .thermal_ok(thermal_ok),
    .state(ctrl_state),
    .motor_run(motor_run),
    .field_enable(field_enable),
    .fault_out(fault_out),
    .relay_56k(),
    .relay_fax(),
    .field_duty(field_duty)
  );

  pwm_gen #(
    .CLK_HZ(CLK_HZ),
    .PWM_HZ(20_000),
    .PWM_BITS(10)
  ) field_pwm_gen (
    .clk(s_axi_aclk),
    .rst(rst),
    .duty(field_duty),
    .pwm(field_pwm)
  );

  sync_control_axi_regs regs (
    .s_axi_aclk(s_axi_aclk),
    .s_axi_aresetn(s_axi_aresetn),
    .s_axi_awaddr(s_axi_awaddr),
    .s_axi_awvalid(s_axi_awvalid),
    .s_axi_awready(s_axi_awready),
    .s_axi_wdata(s_axi_wdata),
    .s_axi_wstrb(s_axi_wstrb),
    .s_axi_wvalid(s_axi_wvalid),
    .s_axi_wready(s_axi_wready),
    .s_axi_bresp(s_axi_bresp),
    .s_axi_bvalid(s_axi_bvalid),
    .s_axi_bready(s_axi_bready),
    .s_axi_araddr(s_axi_araddr),
    .s_axi_arvalid(s_axi_arvalid),
    .s_axi_arready(s_axi_arready),
    .s_axi_rdata(s_axi_rdata),
    .s_axi_rresp(s_axi_rresp),
    .s_axi_rvalid(s_axi_rvalid),
    .s_axi_rready(s_axi_rready),
    .frame_valid(frame_valid),
    .frame_bad(frame_bad),
    .ads_status(status),
    .ch0(ch0),
    .ch1(ch1),
    .ch2(ch2),
    .ch3(ch3),
    .ch4(ch4),
    .ch5(ch5),
    .ch6(ch6),
    .ch7(ch7),
    .good_frames(good_frames),
    .bad_frames(bad_frames),
    .freq_mhz(freq_mhz),
    .freq_locked(freq_locked),
    .ctrl_state(ctrl_state),
    .motor_run(motor_run),
    .field_enable(field_enable),
    .fault_out(fault_out),
    .field_duty(field_duty),
    .start_pulse(start_pulse),
    .stop_pulse(stop_pulse),
    .reset_pulse(reset_pulse),
    .control_reg(control_reg)
  );

  always @(posedge s_axi_aclk) begin
    if (rst) begin
      abs_acc <= 32'd0;
      abs_count <= 10'd0;
      abs_mean <= 16'd0;
      sync_release <= 32'd0;
    end else begin
      if (frame_valid) begin
        abs_acc <= abs_acc + (ch0[15] ? {16'd0, (~ch0 + 16'sd1)} : {16'd0, ch0});
        abs_count <= abs_count + 10'd1;
        if (abs_count == 10'd255) begin
          abs_mean <= abs_acc[31:8];
          abs_acc <= 32'd0;
          abs_count <= 10'd0;
        end
      end

      if (zero_cross && ctrl_state == 4'd6) sync_release <= 32'd40_000;
      else if (sync_release != 0) sync_release <= sync_release - 32'd1;
    end
  end

  assign sync_pulse = sync_release != 0;

endmodule
