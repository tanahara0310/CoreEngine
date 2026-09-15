// リザルトで出す Tips の文言を並べて持つ。リザルトに入るたびに、ResultDirector が次の空でない文言を出す（最後の次は先頭へ戻る）
[DisplayName("リザルトの Tips")]
class ResultTips : ScriptComponent
{
    [DisplayName("Tips")]
    array<string> tips;
}
