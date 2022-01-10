<?php
/* SPDX-License-Identifer: GPL-2.0-only */
/*
 * Copyright (C) 2021  Ammar Faizi <ammarfaizi2@gmail.com>
 */

namespace GreenTea\API;

use PDO;
use GreenTea\APIFoundation;

class GetMessageCountGroup extends APIFoundation
{
	/**
	 * @return array
	 */
	public function getCount(): array
	{
		$pdo = $this->getPDO();
		$date = date("Y-m-d");
		$query = <<<SQL
			SELECT
			gt_groups.name, COUNT(1) AS msg_count
			FROM gt_messages
			INNER JOIN gt_message_content
			ON gt_messages.id = gt_message_content.id
			INNER JOIN gt_groups
			ON gt_groups.id = gt_messages.chat_id WHERE
			gt_message_content.tg_date >= '{$date} 00:00:00' AND
			gt_message_content.tg_date <= '{$date} 23:59:59'
			GROUP BY gt_messages.chat_id
			ORDER BY msg_count DESC;
SQL;
		$st = $pdo->prepare($query);
		$st->execute();
		$this->errorCode = 0;
		return [
			"is_ok" => true,
			"msg"   => NULL,
			"data"  => $st->fetchAll(PDO::FETCH_ASSOC),
		];
	}

	public function getCountStats(string $startDate, ?string $endDate = NULL): array
	{
		$data = [];

		if ($endDate) {
			$epEndDate = strtotime($endDate);
			$endDate = date("Y-m-d", $epEndDate)." 23:59:59";
		} else {
			$endDate = date("Y-m-d")." 23:59:59";
			$epEndDate = time();
		}

		$origEpStartDate = strtotime($startDate);
		$startDate = date("Y-m-d", $origEpStartDate)." 00:00:00";
		$epStartDate = $origEpStartDate;
		while ($epStartDate <= $epEndDate) {
			$data[date("Y-m-d", $epStartDate)] = 0;
			$epStartDate += 3600 * 24;
		}

		$query = <<<SQL
			SELECT
			DATE(tg_date) AS msg_date, COUNT(1) AS nr_msg
			FROM gt_message_content
			WHERE tg_date >= ? AND tg_date <= ?
			GROUP BY msg_date
			ORDER BY msg_date DESC
SQL;
		$pdo = $this->getPDO();
		$st = $pdo->prepare($query);
		$st->execute([$startDate, $endDate]);
		$this->errorCode = 0;
		while ($r = $st->fetch(PDO::FETCH_ASSOC))
			$data[$r["msg_date"]] = (int) $r["nr_msg"];

		return [
			"is_ok" => true,
			"msg"   => NULL,
			"data"  => $data,
		];
	}

	/**
	 * @return int
	 */
	public function getErrorCode(): int
	{
		return $this->errorCode;
	}

	/**
	 * @return bool
	 */
	public function isError(): bool
	{
		return $this->errorCode != 0;
	}
};
