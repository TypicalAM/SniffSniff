<script lang="ts">
	import type { Packet } from '../proto/service';
	export let packet: Packet;

	function formatHexDump(data: Uint8Array): string {
		return Array.from(data)
			.map((byte) => byte.toString(16).padStart(2, '0'))
			.join(' ');
	}
</script>

<div class="bg-muted p-4">
	<div class="grid grid-cols-2 gap-8">
		<!-- Left column - Basic info -->
		<div>
			<h3 class="text-foreground mb-4 font-medium">Basic Information</h3>
			<dl class="space-y-2">
				<div class="grid grid-cols-2 gap-2">
					<dt class="text-muted-foreground">Time</dt>
					<dd class="text-foreground">
						{new Date(Number(packet.captureTime?.seconds) * 1000).toLocaleString()}
					</dd>
				</div>
				<div class="grid grid-cols-2 gap-2">
					<dt class="text-muted-foreground">Source</dt>
					<dd class="text-foreground font-mono">{packet.src}</dd>
				</div>
				<div class="grid grid-cols-2 gap-2">
					<dt class="text-muted-foreground">Destination</dt>
					<dd class="text-foreground font-mono">{packet.dst}</dd>
				</div>
			</dl>
		</div>

		<!-- Right column - protocol's info -->
		<div>
			<h3 class="text-foreground mb-4 font-medium">Protocol Details</h3>
			{#if packet.data.oneofKind === 'ip'}
				<dl class="space-y-2">
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">Version</dt>
						<dd class="text-foreground">IPv4</dd>
					</div>
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">TTL</dt>
						<dd class="text-foreground">{packet.data.ip.ttl}</dd>
					</div>
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">Protocol</dt>
						<dd class="text-foreground">{packet.data.ip.protocol}</dd>
					</div>
					{#if packet.data.ip.next.oneofKind === 'tcp'}
						<div class="mt-4">
							<h4 class="text-foreground mb-2 font-medium">TCP Information</h4>
							<div class="grid grid-cols-2 gap-2">
								<dt class="text-muted-foreground">Source Port</dt>
								<dd class="text-foreground">{packet.data.ip.next.tcp.sourcePort}</dd>
							</div>
							<div class="grid grid-cols-2 gap-2">
								<dt class="text-muted-foreground">Destination Port</dt>
								<dd class="text-foreground">{packet.data.ip.next.tcp.destinationPort}</dd>
							</div>
							<div class="grid grid-cols-2 gap-2">
								<dt class="text-muted-foreground">Sequence</dt>
								<dd class="text-foreground">{packet.data.ip.next.tcp.sequenceNumber}</dd>
							</div>
							<div class="grid grid-cols-2 gap-2">
								<dt class="text-muted-foreground">Flags</dt>
								<dd class="text-foreground">
									{[
										packet.data.ip.next.tcp.syn && 'SYN',
										packet.data.ip.next.tcp.ack && 'ACK',
										packet.data.ip.next.tcp.fin && 'FIN'
									]
										.filter(Boolean)
										.join(' ')}
								</dd>
							</div>
						</div>
					{/if}
				</dl>
			{:else if packet.data.oneofKind === 'arp'}
				<dl class="space-y-2">
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">Operation</dt>
						<dd class="text-foreground">Request</dd>
					</div>
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">Sender IP</dt>
						<dd class="text-foreground">{packet.data.arp.senderIpAddress}</dd>
					</div>
					<div class="grid grid-cols-2 gap-2">
						<dt class="text-muted-foreground">Target IP</dt>
						<dd class="text-foreground">{packet.data.arp.targetIpAddress}</dd>
					</div>
				</dl>
			{/if}
		</div>
	</div>

	{#if packet.data.oneofKind === 'raw'}
		<div class="mt-4">
			<h3 class="text-foreground mb-2 font-medium">Raw Data</h3>
			<div class="bg-background text-foreground overflow-x-auto rounded p-2 font-mono text-sm">
				{formatHexDump(packet.data.raw.payload)}
			</div>
		</div>
	{/if}
</div>
